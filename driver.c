#include "driver.h"
#include "rpc.h"
#include "os.h"

#define MAX_METADATA_SIZE (8 * 1024 * 1024)
#define MAX_CHUNK_SIZE (256 * 1024)

static inline size_t _AlignUp(size_t iValue, size_t iAlignment) {
	size_t iRemainder = iValue % iAlignment;
	if(iRemainder) iValue += iAlignment - iRemainder;
	return iValue;
}

// ===================================================

static void* fsdriver_init(struct fuse_conn_info* pConnection, struct fuse_config* pConfig) {
	pConfig->kernel_cache = 1;
	return NULL;
}

static void fsdriver_destroy(void* pData) {
	fsrpc_disconnect();
	fsrpc_cleanup();
}

static int fsdriver_getattr(const char* sPath, struct stat* pOutput, struct fuse_file_info* pFile) {
	memset(pOutput, 0, sizeof(struct stat));
	if(strcmp(sPath, "/") == 0) {
		pOutput->st_mode = S_IFDIR | 0755;
		pOutput->st_nlink = 2;
		return 0;
	}

	fsrpc_request_t pRequest = fsrpc_create_request(
		"GETATTR", (const char*[]){
			"Path", sPath,
			"Format", "binary-le-1",
			"Max-Size", UINT64_STR(MAX_METADATA_SIZE),
			NULL
		},
		MAX_METADATA_SIZE, 0
	);

	if(!pRequest) return -ENOMEM;
	int iStatus = fsrpc_perform_request(pRequest);
	if(iStatus) {
		fsrpc_free_request(pRequest);
		return iStatus;
	}

	if(pRequest->tResponse.iCursor < 8) {
		fsrpc_free_request(pRequest);
		return -ECONNRESET;
	}

	uint8_t iErrorCode = *(uint8_t*)pRequest->tResponse.pMemory;
	if(iErrorCode) {
		fsrpc_free_request(pRequest);
		return fsrpc_errno(iErrorCode);
	}

	if(pRequest->tResponse.iCursor < 8 + sizeof(struct fsrpc_stat)) {
		fsrpc_free_request(pRequest);
		return -ECONNRESET;
	}

	fsrpc_stat_t pMetadata = pRequest->tResponse.pMemory + 8;
	if(pMetadata->iType == 0) pOutput->st_mode = S_IFDIR | 0755;
	else if(pMetadata->iType == 1) pOutput->st_mode = S_IFREG | 0755;
	else pOutput->st_mode = 0755;

	pOutput->st_nlink = 2;
	pOutput->st_size = pMetadata->iSize;
	pOutput->st_blocks = _AlignUp(pMetadata->iSize, 512) / 512;
	pOutput->st_mtim.tv_sec = pMetadata->tModificationTime.iSeconds;
	pOutput->st_mtim.tv_nsec = pMetadata->tModificationTime.iNanoseconds;

	fsrpc_free_request(pRequest);
	return 0;
}

static int fsdriver_readdir(const char* sPath, void* pOutput, fuse_fill_dir_t lFiller, off_t iOffset, struct fuse_file_info* pFile, enum fuse_readdir_flags xFlags) {
	fsrpc_request_t pRequest = fsrpc_create_request(
		"READDIR", (const char*[]){
			"Path", sPath,
			"Format", "binary-le-1",
			"Max-Size", UINT64_STR(MAX_METADATA_SIZE),
			NULL
		},
		MAX_METADATA_SIZE, 0
	);

	lFiller(pOutput, ".", NULL, 0, 0);
	lFiller(pOutput, "..", NULL, 0, 0);

	int iStatus = fsrpc_perform_request(pRequest);
	if(iStatus) {
		fsrpc_free_request(pRequest);
		return iStatus;
	}

	if(pRequest->tResponse.iCursor < 8) {
		fsrpc_free_request(pRequest);
		return -ECONNRESET;
	}

	uint64_t iTotalEntries = *(uint64_t*)pRequest->tResponse.pMemory;
	void* pNext = pRequest->tResponse.pMemory + 8;
	uint64_t iRemaining = pRequest->tResponse.iCursor - 8;

	while(iTotalEntries) {
		if(iRemaining < sizeof(struct fsrpc_dirent)) break;
		fsrpc_dirent_t pEntry = pNext;
		uint32_t iEntrySize = sizeof(struct fsrpc_dirent) + pEntry->iNameSize + 1;
		uint8_t iRemainder = iEntrySize % 8;
		if(iRemainder) iEntrySize += 8 - iRemainder;

		if(iRemaining < iEntrySize) break;
		if(pEntry->sName[pEntry->iNameSize] != '\0') break;

		lFiller(pOutput, pEntry->sName, NULL, 0, 0);

		--iTotalEntries;
		pNext += iEntrySize;
		iRemaining -= iEntrySize;
	}

	if(iTotalEntries) fprintf(stderr, "readdir: Truncated response: %lu %s remaining\n", iTotalEntries, iTotalEntries == 1 ? "entry" : "entries");
	else if(iRemaining) fprintf(stderr, "readdir: %lu %s not parsed\n", iRemaining, iRemaining == 1 ? "byte was" : "bytes were");
	fsrpc_free_request(pRequest);
	return 0;
}

static int fsdriver_statfs(const char* sPath, struct statvfs* pResponse) {
	memset(pResponse, 0, sizeof(struct statvfs));

	fsrpc_request_t pRequest = fsrpc_create_request(
		"STATVFS",
		(const char*[]){ "Format", "binary-le-1", NULL },
		MAX_METADATA_SIZE, 0
	);

	if(!pRequest) return -ENOMEM;

	int iStatus = fsrpc_perform_request(pRequest);
	if(iStatus) {
		fsrpc_free_request(pRequest);
		return iStatus;
	}

	if(pRequest->tResponse.iCursor < sizeof(struct fsrpc_statvfs)) {
		fsrpc_free_request(pRequest);
		return -ECONNRESET;
	}

	fsrpc_statvfs_t pData = pRequest->tResponse.pMemory;
	pResponse->f_files = pData->iTotalInodes;
	pResponse->f_ffree = pData->iFreeInodes;
	pResponse->f_bsize = 1024;
	pResponse->f_frsize = 1024;
	pResponse->f_blocks = pData->iTotalSpace / pResponse->f_frsize;
	pResponse->f_bfree = pData->iFreeSpace / pResponse->f_frsize;
	pResponse->f_bavail = pData->iFreeSpace / pResponse->f_frsize;

	fsrpc_free_request(pRequest);
	return 0;
}

static int fsrpc_call_perform(fsrpc_request_t pRequest, int8_t bFree) {
	if(!pRequest) return -ENOMEM;

	int iStatus = fsrpc_perform_request(pRequest);
	if(iStatus) {
		fsrpc_free_request(pRequest);
		return iStatus;
	}

	if(pRequest->tResponse.iCursor < 1) {
		fsrpc_free_request(pRequest);
		return -ECONNRESET;
	}

	iStatus = fsrpc_errno(*(uint8_t*)pRequest->tResponse.pMemory);
	if(bFree || iStatus) fsrpc_free_request(pRequest);
	return iStatus;
}

#define fsrpc_call_nodata(sEndpoint, ...) fsrpc_call_perform(fsrpc_create_request((sEndpoint), (const char*[]){ __VA_ARGS__, NULL}, MAX_METADATA_SIZE, 0), 1);
#define fsrpc_call_path(sEndpoint, sPath) fsrpc_call_nodata(sEndpoint, "Path", sPath, "Format", "binary-le-1")

static int fsdriver_unlink(const char* sPath) {
	return fsrpc_call_path("UNLINK", sPath);
}

static int fsdriver_rmdir(const char* sPath) {
	return fsrpc_call_path("RMDIR", sPath);
}

static int fsdriver_mkdir(const char* sPath, mode_t xMode) {
	return fsrpc_call_nodata(
		"MKDIR",
		"Path", sPath,
		"Mode", UINT32_STR(xMode),
		"Format", "binary-le-1"
	);
}

static int fsdriver_create(const char* sPath, mode_t xMode, struct fuse_file_info* pFile) {
	return fsrpc_call_nodata(
		"OPEN",
		"Path", sPath,
		"Access", UINT32_STR(O_RDWR),
		"Mode", UINT32_STR(xMode),
		"Trunc", "0",
		"Create", "1",
		"Excl", "1",
		"Format", "binary-le-1"
	);
}

static int fsdriver_open(const char* sPath, struct fuse_file_info* pFile) {
	return fsrpc_call_nodata(
		"OPEN",
		"Path", sPath,
		"Access", UINT32_STR(pFile->flags & O_ACCMODE),
		"Mode", UINT32_STR(0777),
		"Trunc", UINT32_STR(!!(pFile->flags & O_TRUNC)),
		"Create", "0",
		"Excl", "0",
		"Format", "binary-le-1"
	);
}

static int fsdriver_read(const char* sPath, char* pBuffer, size_t iSize, off_t iOffset, struct fuse_file_info* pFile) {
	//printf("READ %lu %lu\n", iOffset, iSize);

	fsrpc_request_t pRequest = fsrpc_create_request(
		"READ",
		(const char*[]){
			"Path", sPath,
			"Offset", UINT64_STR(iOffset),
			"Size", UINT64_STR(iSize),
			"Format", "binary-le-1",
			NULL
		}, 8 + iSize, 0
	);

	int iStatus = fsrpc_call_perform(pRequest, 0);
	if(iStatus) return iStatus;

	if(pRequest->tResponse.iCursor < 8) {
		fsrpc_free_request(pRequest);
		return -EIO;
	}

	iStatus = pRequest->tResponse.iCursor - 8;
	if(iStatus) memcpy(pBuffer, pRequest->tResponse.pMemory + 8, iStatus);
	fsrpc_free_request(pRequest);
	return iStatus;
}

static int fsdriver_write(const char* sPath, const char* pBuffer, size_t iSize, off_t iOffset, struct fuse_file_info* pFile) {
	size_t iRemaining = iSize;
	while(iRemaining) {
		size_t iChunkSize = MAX_CHUNK_SIZE < iRemaining ? MAX_CHUNK_SIZE : iRemaining;
		//printf("WRITE %lu %lu\n", iOffset, iChunkSize);

		fsrpc_request_t pRequest = fsrpc_create_request(
			"WRITE",
			(const char*[]){ "Path", sPath, "Offset", UINT64_STR(iOffset), "Format", "binary-le-1", NULL },
			MAX_METADATA_SIZE, 0
		);

		if(!pRequest) return -ENOMEM;
		if(fsrpc_upload_buffer(pRequest, pBuffer, iChunkSize)) {
			fsrpc_free_request(pRequest);
			return -EIO;
		}

		int iStatus = fsrpc_call_perform(pRequest, 1);
		if(iStatus) return iStatus;

		iOffset += iChunkSize;
		iRemaining -= iChunkSize;
	}

	return iSize;
}

/*static int fsdriver_truncate(const char* sPath, off_t iSize, struct fuse_file_info* pFile) {
	return fsrpc_call_nodata(
		"TRUNCATE",
		"Path", sPath,
		"Size", UINT64_STR(iSize),
		"Format", "binary-le-1",
	);
}*/

const struct fuse_operations fsdriver_operations = {
	.init           = fsdriver_init,
	.destroy        = fsdriver_destroy,
	.getattr	= fsdriver_getattr,
	.readdir	= fsdriver_readdir,
	.statfs		= fsdriver_statfs,
	.unlink		= fsdriver_unlink,
	.rmdir		= fsdriver_rmdir,
	.mkdir		= fsdriver_mkdir,
	.open		= fsdriver_open,
	.create		= fsdriver_create,
	.read		= fsdriver_read,
	.write		= fsdriver_write,
	//.truncate	= fsdriver_truncate,
};
