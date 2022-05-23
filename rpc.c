#include "rpc.h"
#include <string.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <errno.h>

static char* g_sTokenHeader = NULL;
static char* g_sRootHeader = NULL;
static char* g_sURL = NULL;
static bool g_bDebug = false;

// ===================================================
// curl
// ===================================================

static size_t curl_membuffer_writecb(void* pData, size_t iSize, size_t iBlocks, struct membuffer* pBuffer) {
	//printf("Writing %lu\n", iSize * iBlocks);
	iSize *= iBlocks;

	if(pBuffer->iCursor + iSize > pBuffer->iMaxSize) {
		printf("Response buffer overflow\n");
		return 0;
	}

	if(pBuffer->iCursor + iSize > pBuffer->iSize) {
		void* pMemory = realloc(pBuffer->pMemory, pBuffer->iCursor + iSize);
		if(!pMemory) {
			printf("realloc failed\n");
			return 0;
		}

		pBuffer->pMemory = pMemory;
		pBuffer->iSize = pBuffer->iCursor + iSize;
	}

	memcpy(pBuffer->pMemory + pBuffer->iCursor, pData, iSize);
	pBuffer->iCursor += iSize;
	return iSize;
}

static size_t curl_membuffer_readcb(void* pData, size_t iSize, size_t iBlocks, struct membuffer* pBuffer) {
	//printf("Reading %lu\n", iSize * iBlocks);
	iSize *= iBlocks;
	size_t iRemaining = pBuffer->iSize - pBuffer->iCursor;
	iSize = iRemaining < iSize ? iRemaining : iSize;

	memcpy(pData, pBuffer->pMemory + pBuffer->iCursor, iSize);
	pBuffer->iCursor += iSize;
	return iSize;
}

static int curl_progresscb(void* this, double dltotal, double dlnow, double ultotal, double ulnow) {
	//printf("DOWNLOADED: %.2f KB  TOTAL SIZE: %.2f KB  UP: %.2f KB  UP-TOTAL: %.2f KB\n", dlnow / 1024, dltotal / 1024, ulnow / 1024, ultotal / 1024);
	return 0;
}

// ===================================================
// RPC
// ===================================================

int8_t fsrpc_set_token(const char* sToken) {
	if(g_sTokenHeader) free(g_sTokenHeader);
	if(sToken && !(g_sTokenHeader = strdup(sToken))) return -1;
	return 0;
}

int8_t fsrpc_set_root(const char* sRoot) {
	if(g_sRootHeader) free(g_sRootHeader);
	if(sRoot && !(g_sRootHeader = strdup(sRoot))) return -1;
	return 0;
}

int8_t fsrpc_set_endpoint(const char* sEndpoint) {
	if(!(g_sURL = strdup(sEndpoint))) return -1;
	return 0;
}

void fsrpc_set_debug(bool bDebug) {
	g_bDebug = bDebug;
}

int8_t fsrpc_init() {
	if(curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) return -1;
	return 0;
}

void fsrpc_cleanup() {
	if(g_sTokenHeader) {
		free(g_sTokenHeader);
		g_sTokenHeader = NULL;
	}

	if(g_sRootHeader) {
		free(g_sRootHeader);
		g_sRootHeader = NULL;
	}

	if(g_sURL) {
		free(g_sURL);
		g_sURL = NULL;
	}

	curl_global_cleanup();
}

static int8_t _fsrpc_add_header(struct fsrpc_request* pRequest, const char* sKey, const char* sValue) {
	const char* sX = "X-";
	const char* sColon = ": ";

	char* sEncodedValue = curl_easy_escape(pRequest->hRequest, sValue, strlen(sValue));
	if(!sEncodedValue) return -1;

	char* sHeader = malloc(
		strlen(sX) +
		strlen(sKey) +
		strlen(sColon) +
		strlen(sEncodedValue) +
		1
	);

	if(!sHeader) {
		curl_free(sEncodedValue);
		return -1;
	}

	strcpy(sHeader, sX);
	strcat(sHeader, sKey);
	strcat(sHeader, sColon);
	strcat(sHeader, sEncodedValue);
	curl_free(sEncodedValue);

	struct curl_slist* pListPointer = curl_slist_append(pRequest->pHeaders, sHeader);
	free(sHeader);
	if(!pListPointer) return -2;

	pRequest->pHeaders = pListPointer;
	return 0;
}

static int8_t _fsrpc_parse_headers(struct fsrpc_request* pRequest, const char** aHeaders) {
	for(;;) {
		const char* sKey = *aHeaders++;
		if(!sKey) break;

		const char* sValue = *aHeaders++;
		if(!sValue) return -1;

		if(_fsrpc_add_header(pRequest, sKey, sValue)) return -2;
	}

	return 0;
}

fsrpc_request_t fsrpc_create_request(const char* sMethod, const char** aArguments, uintmax_t iMaxSize, uint8_t xFlags) {
	if(!g_sTokenHeader || !g_sURL) return NULL;

	struct fsrpc_request* pRequest = calloc(1, sizeof(struct fsrpc_request));
	if(!pRequest) return NULL;

	pRequest->tResponse.iMaxSize = iMaxSize;
	if(xFlags & FSRPC_EXACT) {
		if(!(pRequest->tResponse.pMemory = malloc(iMaxSize))) goto error;
		pRequest->tResponse.iSize = iMaxSize;
	}

	pRequest->hRequest = curl_easy_init();
	if(!pRequest->hRequest) {
		printf("curl init failed\n");
		goto error;
	}

	if(aArguments && _fsrpc_parse_headers(pRequest, aArguments)) goto error;
	if(g_sTokenHeader && _fsrpc_add_header(pRequest, "Token", g_sTokenHeader)) goto error;
	if(g_sRootHeader && _fsrpc_add_header(pRequest, "Root", g_sRootHeader)) goto error;

	if(curl_easy_setopt(pRequest->hRequest, CURLOPT_URL, g_sURL) != CURLE_OK) goto error;
	if(curl_easy_setopt(pRequest->hRequest, CURLOPT_CUSTOMREQUEST, sMethod) != CURLE_OK) goto error;
	if(curl_easy_setopt(pRequest->hRequest, CURLOPT_FAILONERROR, 1) != CURLE_OK) goto error;
	if(curl_easy_setopt(pRequest->hRequest, CURLOPT_NOPROGRESS, !g_bDebug) != CURLE_OK) goto error;
	if(curl_easy_setopt(pRequest->hRequest, CURLOPT_HTTPHEADER, pRequest->pHeaders) != CURLE_OK) goto error;
	if(curl_easy_setopt(pRequest->hRequest, CURLOPT_VERBOSE, g_bDebug) != CURLE_OK) goto error;

	if(curl_easy_setopt(pRequest->hRequest, CURLOPT_WRITEFUNCTION, curl_membuffer_writecb) != CURLE_OK) goto error;
	if(curl_easy_setopt(pRequest->hRequest, CURLOPT_WRITEDATA, &pRequest->tResponse) != CURLE_OK) goto error;

	if(curl_easy_setopt(pRequest->hRequest, CURLOPT_PROGRESSFUNCTION, curl_progresscb) != CURLE_OK) goto error;
	if(curl_easy_setopt(pRequest->hRequest, CURLOPT_PROGRESSDATA, NULL) != CURLE_OK) goto error;

	return pRequest;

	error:
	fsrpc_free_request(pRequest);
	return NULL;
}

int fsrpc_upload_buffer(fsrpc_request_t pRequest, const void* pData, uint64_t iSize) {
	pRequest->tRequestBody.pMemory = (void*)pData;
	pRequest->tRequestBody.iSize = iSize;

	if(curl_easy_setopt(pRequest->hRequest, CURLOPT_UPLOAD, 1) != CURLE_OK) return -1;
	if(curl_easy_setopt(pRequest->hRequest, CURLOPT_INFILESIZE_LARGE, (curl_off_t)iSize) != CURLE_OK) return -1;

	if(curl_easy_setopt(pRequest->hRequest, CURLOPT_READFUNCTION, curl_membuffer_readcb) != CURLE_OK) return -1;
	if(curl_easy_setopt(pRequest->hRequest, CURLOPT_READDATA, &pRequest->tRequestBody) != CURLE_OK) return -1;

	return 0;
}

int fsrpc_perform_request(fsrpc_request_t pRequest) {
	CURLcode iError = curl_easy_perform(pRequest->hRequest);
	if(iError == CURLE_HTTP_RETURNED_ERROR) {
		long iStatusCode = 0;
		curl_easy_getinfo(pRequest->hRequest, CURLINFO_RESPONSE_CODE, &iStatusCode);
		switch(iStatusCode) {
			case 403: fprintf(stderr, "Invalid access token\n"); break;
			default: fprintf(stderr, "HTTP error: %lu\n", iStatusCode);
		}

		return -ECONNRESET;

	} else if(iError != CURLE_OK) {
		fprintf(stderr, "curl: %s\n", curl_easy_strerror(iError));
		return -ECONNRESET;
	}

	return 0;
}

void fsrpc_free_request(fsrpc_request_t pRequest) {
	if(pRequest->pHeaders) curl_slist_free_all(pRequest->pHeaders);
	if(pRequest->hRequest) curl_easy_cleanup(pRequest->hRequest);
	if(pRequest->tResponse.pMemory) free(pRequest->tResponse.pMemory);
	free(pRequest);
}

int fsrpc_connect() {
	fsrpc_request_t pRequest = fsrpc_create_request("INIT", NULL, 512 * 1024, 0);
	if(!pRequest) return -ENOMEM;

	int iStatus = fsrpc_perform_request(pRequest);

	fsrpc_free_request(pRequest);
	return iStatus;
}

void fsrpc_disconnect() {
	fsrpc_request_t pRequest = fsrpc_create_request("DESTROY", NULL, 0, 0);
	if(pRequest) {
		fsrpc_perform_request(pRequest);
		fsrpc_free_request(pRequest);
	}
}

int fsrpc_errno(uint8_t iError) {
	switch(iError) {
		case 0: return 0;
		case 1: return -ENOENT;
		case 2: return -EACCES;
		case 3: return -ENOTDIR;
		case 4: return -EIO;
		case 5: return -ENOTSUP;
		case 6: return -EISDIR;
		case 7: return -EEXIST;
		case 8: return -EDQUOT;
		case 9: return -ENOTEMPTY;
		case 10: return -EROFS;
		case 11: return -ENOSPC;
		case 12: return -EAGAIN;

		default: return -EIO;
	}
}
