#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <curl/curl.h>

struct membuffer {
	void* pMemory;
	uintmax_t iCursor;
	uintmax_t iSize;
	uintmax_t iMaxSize;
};

enum fsrpc_create_flags {
	FSRPC_EXACT = 1 << 0,
};

struct fsrpc_timespec {
	uint64_t iSeconds;
	uint64_t iNanoseconds;
};

#pragma pack(push, 1)
typedef struct fsrpc_stat {
	uint64_t iSize;
	struct fsrpc_timespec tAccessTime;
	struct fsrpc_timespec tModificationTime;
	struct fsrpc_timespec tMetadataChangeTime;
	struct fsrpc_timespec tCreateTime;
	uint32_t iMappedUser;
	uint32_t iMappedGroup;
	uint16_t xPermissionBits;
	uint8_t iType;
} *fsrpc_stat_t;

typedef struct fsrpc_dirent {
	uint64_t iSize;
	struct fsrpc_timespec tAccessTime;
	struct fsrpc_timespec tModificationTime;
	struct fsrpc_timespec tMetadataChangeTime;
	struct fsrpc_timespec tCreateTime;
	uint32_t iMappedUser;
	uint32_t iMappedGroup;
	uint16_t xPermissionBits;
	uint8_t iType;
	uint8_t iNameSize;
	char sName[0];
} *fsrpc_dirent_t;

typedef struct fsrpc_statvfs {
	uint64_t iTotalSpace;
	uint64_t iFreeSpace;

	uint64_t iTotalInodes;
	uint64_t iFreeInodes;
} *fsrpc_statvfs_t;
#pragma pack(pop)
//_Static_assert(sizeof(struct fsrpc_dirent) % 8 == 0);

typedef struct fsrpc_request {
	struct curl_slist* pHeaders;
	CURL* hRequest;

	struct membuffer tRequestBody;
	struct membuffer tResponse;
} *fsrpc_request_t;

struct uint32_str { char s[10 + 1]; };
struct uint64_str { char s[20 + 1]; };

static inline struct uint32_str uint32_str(uint32_t iValue) {
	struct uint32_str tResult;
	snprintf(tResult.s, 10 + 1, "%u", iValue);
	return tResult;
}

static inline struct uint64_str uint64_str(uint64_t iValue) {
	struct uint64_str tResult;
	snprintf(tResult.s, 20 + 1, "%lu", iValue);
	return tResult;
}

#define UINT32_STR(iValue) (uint32_str((iValue)).s)
#define UINT64_STR(iValue) (uint64_str((iValue)).s)

int8_t fsrpc_set_token(const char* sToken);
int8_t fsrpc_set_root(const char* sRoot);
int8_t fsrpc_set_endpoint(const char* sEndpoint);
void fsrpc_set_debug(bool bDebug);
int8_t fsrpc_init();
void fsrpc_cleanup();
fsrpc_request_t fsrpc_create_request(const char* sMethod, const char** aHeaders, uintmax_t iMaxSize, uint8_t xFlags);
int fsrpc_upload_buffer(fsrpc_request_t pRequest, const void* pData, uint64_t iSize);
int fsrpc_perform_request(fsrpc_request_t pRequest);
void fsrpc_free_request(fsrpc_request_t pRequest);
int fsrpc_connect();
void fsrpc_disconnect();
int fsrpc_errno(uint8_t iError);
