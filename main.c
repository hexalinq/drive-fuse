#include "rpc.h"
#include "driver.h"
#include "os.h"

/*
    Hexalinq Drive FUSE driver
    Copyright (C) 2022 Hexalinq <info@hexalinq.com> <https://hexalinq.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2 as
    published by the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

static struct Options {
	const char* sToken;
	const char* sTokenPath;
	int bShowHelp;
	int bDebug;
} tOptions;

#define OPTION(t, p) { t, offsetof(struct Options, p), 1 }
static const struct fuse_opt option_spec[] = {
	OPTION("token=%s", sToken),
	OPTION("token-file=%s", sTokenPath),
	OPTION("debug", bDebug),
	OPTION("-h", bShowHelp),
	OPTION("--help", bShowHelp),
	FUSE_OPT_END
};

static void show_help(const char* progname) {
	printf("Hexalinq Drive FUSE driver\n");
	printf("Copyright (C) 2022 Hexalinq <info@hexalinq.com> <https://hexalinq.com>\n");
	printf("\n");
	printf("Usage: %s [options] -o<token=ACCESS_TOKEN|token-file=PATH> <remote-path> <mountpoint>\n\n", progname);
	printf("Filesystem specific options:\n"
	       "    -o token=<s>         Hexalinq Drive access token\n"
	       "    -o token-file=<s>    File to read the access token from\n"
	       "    -o debug             Keep the process in foreground and turn on debugging output\n"
	       "    --help               Display the help message\n"
	       "\n");
}

static char* _ReadFile(const char* sPath) {
	int iFD = open(sPath, O_RDONLY | O_NOCTTY);
	if(iFD < 0) {
		perror("open");
		return NULL;
	}

	off_t iSize = lseek(iFD, 0, SEEK_END);
	if(iSize < 0 || lseek(iFD, 0, SEEK_SET)) {
		perror("lseek");
		close(iFD);
		return NULL;
	}

	char* sData = malloc(iSize + 1);
	if(!sData) {
		printf("Out of memory\n");
		close(iFD);
		return NULL;
	}

	ssize_t iRead = read(iFD, sData, iSize);
	close(iFD);
	if(iRead != iSize) {
		perror("read");
		free(sData);
		return NULL;
	}

	sData[iSize] = '\0';

	char* pNL = strchr(sData, '\n');
	if(pNL) *pNL = '\0';
	pNL = strchr(sData, '\r');
	if(pNL) *pNL = '\0';

	return sData;
}

#define crash(fmt, ...) do { \
	fprintf(stderr, "\033[38;2;255;0;0m\033[1m%s:%d | " fmt "\033[0m\n", __FILE__, __LINE__, ##__VA_ARGS__); \
	fflush(stdout); \
	fflush(stderr); \
	exit(1); \
	for(;;); \
} while(0)

static int _HandlePath(struct fuse_args* pArgs) {
	if(pArgs->argc < 3) return 1;
	char* sPath = pArgs->argv[pArgs->argc - 2];
	if(!strlen(sPath) || sPath[0] != '/') return 1;
	if(fsrpc_set_root(sPath)) return 1;
	free(sPath);
	pArgs->argv[pArgs->argc - 2] = pArgs->argv[pArgs->argc - 1];
	--pArgs->argc;
	return 0;
}

static int _HandleArgs(struct fuse_args* args) {
	if(fsrpc_init()) return 1;
	if(fsrpc_set_endpoint(SCHEME "://" ENDPOINT "/fsapi")) return 1;

	if(fuse_opt_parse(args, &tOptions, option_spec, NULL)) crash("fuse_opt_parse");
	if(fuse_opt_insert_arg(args, 1, "-osubtype=hexalinq-drive,fsname=" ENDPOINT)) crash("fuse_opt_add_arg");

	char* sToken = NULL;
	if(tOptions.sToken) sToken = strdup(tOptions.sToken);
	else if(tOptions.sTokenPath) {
		sToken = _ReadFile(tOptions.sTokenPath);
		if(!sToken) return 1;
	}

	if(!tOptions.bShowHelp && !sToken) {
		show_help(args->argv[0]);
		return 1;
	}

	if(tOptions.bShowHelp || !sToken) {
		show_help(args->argv[0]);
		if(fuse_opt_add_arg(args, "--help")) crash("fuse_opt_add_arg");
		args->argv[0][0] = '\0';
		return 0;
	}

	if(_HandlePath(args)) {
		show_help(args->argv[0]);
		return 1;
	}

	if(tOptions.bDebug) {
		if(fuse_opt_add_arg(args, "-f")) crash("fuse_opt_add_arg");
		fsrpc_set_debug(1);
	}

	if(fsrpc_set_token(sToken)) crash("fsrpc_set_token");
	if(fsrpc_connect()) crash("fsrpc_connect");

	return 0;
}

int main(int argc, char *argv[]) {
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	if(_HandleArgs(&args)) crash("Invalid arguments");
	if(fuse_main(args.argc, args.argv, &fsdriver_operations, NULL)) crash("libfuse error");
	fuse_opt_free_args(&args);
	return 0;
}
