#include <ncurses.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <bits/local_lim.h>

// Macro defined at: https://github.com/mcdaniel/curses_tutorial#colors
#define RGB_TO_CURSES(x) ((int)((float)x*3.90625))  // 1000/256 conversion
#define DIR 1
#define FILE 2

typedef struct {
	const char *name;
	const char *sshId;
} AgIdent;

void makeIfMissing(const char *path, int type) {
	if (type != FILE && type != DIR || access(path, R_OK) != -1) return;

	const char *makeFormatStr = type == FILE 
		? "touch %s"
		: "mkdir -p %s";

	char *makeCmd = (char *)malloc(
		strlen(makeFormatStr) - 2 + strlen(path)
	);

	sprintf(makeCmd, makeFormatStr, path);
	system(makeCmd);

	free(makeCmd);
}

char *getCfgPath() {
	const char *userHomeFormatStr = "/home/%s/";
	const char *configDirPath = ".config/start-ag/";
	const char *configFilePath = "config";

	size_t configDirLen = strlen(configDirPath);
	size_t configFileLen = strlen(configFilePath);

	char *loginName = getlogin();
	size_t homePathLen = strlen(userHomeFormatStr) - 2 + strlen(loginName);
	size_t buffSize = homePathLen + configDirLen + configFileLen;
	char *cfgPathBuffer = (char *)calloc(1, buffSize);

	sprintf(cfgPathBuffer, userHomeFormatStr, loginName);

	char *tmpCfgDirPath = (char *)calloc(1, homePathLen + configDirLen);
	strncat(tmpCfgDirPath, cfgPathBuffer, homePathLen);
	strncat(tmpCfgDirPath, configDirPath, configDirLen);

	makeIfMissing(tmpCfgDirPath, DIR);

	strncpy(cfgPathBuffer, tmpCfgDirPath, homePathLen + configDirLen);
	free(tmpCfgDirPath);

	strncat(cfgPathBuffer, configFilePath, configFileLen);

	makeIfMissing(cfgPathBuffer, FILE);

	return cfgPathBuffer;
}

int main() {
	char *cfgPath = getCfgPath();
	printf("%s\n", cfgPath);

	free(cfgPath);
	return 0;
}
