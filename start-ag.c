#include <ncurses.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

// Macro defined at: https://github.com/mcdaniel/curses_tutorial#colors
#define RGB_TO_CURSES(x) ((int)((float)x*3.90625))  // 1000/256 conversion
#define D 1
#define F 2
#define SSH_IGNORE_MAP_SIZE 4
#define DEFAULT_AG_ID_NO 3
#define ENTRY_FORMAT_STR "%s:%s\n"
#define ENTRY_ID_MAX 256

typedef struct {
	const char *name;
	const char *sshId;
} AgentId;

typedef struct {
	size_t noIds;
	size_t maxIds;
	unsigned char modified;
	AgentId **agentIds;
} AgentIdCollection;

const char *sshIgnoreMapSize[SSH_IGNORE_MAP_SIZE] = {
	"agent",
	"authorized_keys",
	"known_hosts",
	"known_hosts.old",
};

AgentIdCollection *genAgentIdColl();
AgentId *searchAgentId(AgentIdCollection *coll, const char *sshId);
unsigned char addAgentId(const char *name, const char *sshId, AgentIdCollection *coll);
void freeAgentIdColl(FILE *cfgFile, AgentIdCollection *coll);

void makeIfMissing(const char *path, int type);
char *getHomePath(size_t *outputLen);

char *getCfgPath();
unsigned char populateConfig(FILE *cfgFile, AgentIdCollection *coll);
unsigned char parseConfig(FILE *cfgFile, AgentIdCollection *coll);

int main() {
	char *cfgPath = getCfgPath();
	size_t cfgPathLen = strlen(cfgPath);
	FILE *cfgFile = fopen(cfgPath, "a+");
	
	AgentIdCollection *idColl = genAgentIdColl();
	char *initMsg = (char *)calloc(1, 1024);
;
	// TODO: Add some kind of logging in the
	// TUI to inform the user that the config
	// file has been populated with the default
	// configuration.
	if (fseek(cfgFile, 0, SEEK_END) == 0 && ftell(cfgFile) == 0) {
		populateConfig(cfgFile, idColl);
		strncpy(initMsg, "Populated config file with default values at: ", 47);
	}
	rewind(cfgFile);

	if (idColl->noIds == 0) {
		parseConfig(cfgFile, idColl);
		strncpy(initMsg, "Loaded found ssh ids at: ", 70);
	}
	strncat(initMsg, cfgPath, cfgPathLen);

	printf("\033[38:5:70m%s\033[m\n", initMsg);
	free(initMsg);

	freeAgentIdColl(cfgFile, idColl);
	free(cfgPath);
	fclose(cfgFile);
	return 0;
}

AgentIdCollection *genAgentIdColl() {
	AgentIdCollection *coll = (AgentIdCollection *)calloc(1, sizeof(AgentIdCollection));
	coll->noIds = coll->modified = 0;
	coll->maxIds = DEFAULT_AG_ID_NO;
	coll->agentIds = (AgentId **)calloc(coll->maxIds, sizeof(AgentId *));
	return coll;
}

AgentId *searchAgentId(AgentIdCollection *coll, const char *sshId) {
	AgentId *target, *current;
	target = current = NULL;

	for (size_t i = 0; target == NULL && i < coll->noIds; i++) {
		current = coll->agentIds[i];
		if (strncmp(sshId, current->sshId, strlen(current->sshId)) == 0)
			target = current;
	}

	return target;
}

unsigned char addAgentId(const char *name, const char *sshId, AgentIdCollection *coll) {
	if (searchAgentId(coll, name) != NULL) return 1;

	AgentId *newAgent = (AgentId *)calloc(1, sizeof(AgentId));
	newAgent->name = name;
	newAgent->sshId = sshId;

	if (coll->noIds == coll->maxIds) {
		coll->agentIds = reallocarray(coll->agentIds, coll->noIds+1, sizeof(AgentId **));
		coll->maxIds = coll->noIds;
	}

	coll->agentIds[coll->noIds++] = newAgent;
	return 0;
}

void freeAgentIdColl(FILE *cfgFile, AgentIdCollection *coll) {
	AgentId *current;
	unsigned char mod = coll->modified;
	if (mod) cfgFile = freopen(NULL, "w", cfgFile);

	for (size_t i = 0; i < coll->noIds; i++) {
		current = coll->agentIds[i];
		if (mod) fprintf(cfgFile, ENTRY_FORMAT_STR, current->name, current->sshId);
		free(current);
	}
	free(coll);
}

void makeIfMissing(const char *path, int type) {
	if ((type != F && type != D) || access(path, R_OK) != -1) return;

	const char *makeFormatStr = type == F
		? "touch %s"
		: "mkdir -p %s";

	// -2 for %s + 1 for \0 = -1
	char *makeCmd = (char *)malloc(
		strlen(makeFormatStr) - 1 + strlen(path)
	);

	sprintf(makeCmd, makeFormatStr, path);
	system(makeCmd);

	free(makeCmd);
}

char *getHomePath(size_t *outputLen) {
	const char *userHomeFormatStr = "/home/%s/";

	char *loginName = getlogin();
	size_t homePathLen = strlen(userHomeFormatStr) - 1 + strlen(loginName);
	char *homePathBuff = (char *)malloc(homePathLen);

	sprintf(homePathBuff, userHomeFormatStr, loginName);
	if (outputLen != NULL) *outputLen = homePathLen;

	return homePathBuff;
}

char *getCfgPath() {
	const char *configDirPath = ".config/start-ag/";
	const char *configFilePath = "agents";

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

unsigned char populateConfig(FILE *cfgFile, AgentIdCollection *coll) {
	if (cfgFile == NULL) {
		fprintf(stderr,
		  "\033[38:5:124m[ERROR]\033[m: unable to open the configuration file. "
		  "Make sure to retrieve the path and manage the FILE ptr safely."
		);
		return 1;
	}

	const char *sshPathFormatStr = "%s.ssh/";

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

		fprintf(cfgFile, ENTRY_FORMAT_STR, entryName, entryName);
		addAgentId(entryName, entryName, coll);
	}

	closedir(sshDir);
	return 0;
}

unsigned char parseConfig(FILE *cfgFile, AgentIdCollection *coll) {
	// ':' + '\0' = 2
	size_t maxLength = 2 * ENTRY_ID_MAX + 2;
	char *line = (char *)calloc(1, maxLength);
	char *name, *sshId;

	while (getline(&line, &maxLength, cfgFile) != EOF) {
		name = strtok(line, ":");
		sshId = strtok(NULL, "\n");
		addAgentId(name, sshId, coll);
	}

	free(line);
	return 0;
}
