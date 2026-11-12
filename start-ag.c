#include <ncurses.h>
#include <stdio.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

// Macro defined at: https://github.com/mcdaniel/curses_tutorial#colors
#define RGB_TO_CURSES(x) ((int)((float)x*3.90625))  // 1000/256 conversion
#define D 1
#define F 2
#define SSH_IGNORE_MAP_SIZE 4

typedef struct {
	const char *name;
	const char *sshId;
} AgIdent;

const char *sshIgnoreMapSize[SSH_IGNORE_MAP_SIZE] = {
	"agent",
	"authorized_keys",
	"known_hosts",
	"known_hosts.old",
};

void makeIfMissing(const char *path, int type) {
	if (type != F && type != D || access(path, R_OK) != -1) return;

	const char *makeFormatStr = type == F
		? "touch %s"
		: "mkdir -p %s";

	char *makeCmd = (char *)malloc(
		strlen(makeFormatStr) - 2 + strlen(path)
	);

	sprintf(makeCmd, makeFormatStr, path);
	system(makeCmd);

	free(makeCmd);
}

char *getHomePath(size_t *outputLen) {
	const char *userHomeFormatStr = "/home/%s/";

	char *loginName = getlogin();
	size_t homePathLen = strlen(userHomeFormatStr) - 2 + strlen(loginName);
	char *homePathBuff = (char *)malloc(homePathLen);

	sprintf(homePathBuff, userHomeFormatStr, loginName);
	if (outputLen != NULL) *outputLen = homePathLen;

	return homePathBuff;
}

char *getCfgPath() {
	const char *configDirPath = ".config/start-ag/";
	const char *configFilePath = "config";

	size_t configDirLen = strlen(configDirPath);
	size_t configFileLen = strlen(configFilePath);
	size_t homePathLen;

	char *homePath = getHomePath(&homePathLen);

	size_t buffSize = homePathLen + configDirLen + configFileLen;
	char *cfgPathBuffer = (char *)calloc(1, buffSize);
	strncpy(cfgPathBuffer, homePath, homePathLen);

	free(homePath);

	char *tmpCfgDirPath = (char *)calloc(1, homePathLen + configDirLen);
	strncat(tmpCfgDirPath, cfgPathBuffer, homePathLen);
	strncat(tmpCfgDirPath, configDirPath, configDirLen);

	makeIfMissing(tmpCfgDirPath, D);

	strncpy(cfgPathBuffer, tmpCfgDirPath, homePathLen + configDirLen);
	free(tmpCfgDirPath);

	strncat(cfgPathBuffer, configFilePath, configFileLen);

	makeIfMissing(cfgPathBuffer, F);

	return cfgPathBuffer;
}

unsigned char populateConfig(FILE *cfgFile) {
	if (cfgFile == NULL) {
		fprintf(stderr,
		  "\033[38:5:124m[ERROR]\033[m: unable to open the configuration file. "
		  "Make sure to retrieve the path and manage the FILE ptr safely."
		);
		return 1;
	}

	const char *sshPathFormatStr = "%s.ssh/";
	const char *sshIdMapFormatStr = "%s:%s\n";

	size_t homePathLen;
	char *homePath = getHomePath(&homePathLen);
	char *sshPathBuff = (char *)calloc(1, strlen(sshPathFormatStr) - 2 + homePathLen);

	sprintf(sshPathBuff, sshPathFormatStr, homePath);
	
	makeIfMissing(sshPathBuff, D);

	DIR *sshDir = opendir(sshPathBuff);
	free(sshPathBuff);
	struct dirent *entry;

	while ((entry = readdir(sshDir)) != NULL) {
		char *entryName = entry->d_name;
		// Ignore public keys
		if (strstr(entryName, ".pub") != NULL) continue;
		// Ignore relative directories
		if (strncmp(entryName, "..", 2) == 0 || strncmp(entryName, ".", 1) == 0) continue;

		// Ignore known non-id files
		size_t i = 0;
		for (;
			strncmp(entryName, sshIgnoreMapSize[i], strlen(sshIgnoreMapSize[i])) != 0 &&
			i < SSH_IGNORE_MAP_SIZE;
			i++);
		if (i < SSH_IGNORE_MAP_SIZE) continue;

		fprintf(cfgFile, sshIdMapFormatStr, entryName, entryName);
	}

	closedir(sshDir);
	return 0;
}

int main() {
	char *cfgPath = getCfgPath();
	FILE *cfgFile = fopen(cfgPath, "a");

	free(cfgPath);
	return 0;
}
