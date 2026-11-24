#define _XOPEN_SOURCE_EXTENDED
#include <ncursesw/ncurses.h>
#include <wchar.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>
#include <assert.h>
#include <math.h>

// Macro defined at: https://github.com/mcdaniel/curses_tutorial#colors
#define RGB_TO_CURSES(x) ((int)((float)x*3.90625))  // 1000/256 conversion
#define D 1
#define F 2
#define SSH_IGNORE_MAP_SIZE 4
#define DEFAULT_AG_ID_NO 3
#define ENTRY_FORMAT_STR "%s:%s\n"
#define ENTRY_ID_MAX 256

#define ENTRY_PADDING 5

#define LEFT_SIDE 			0
#define RIGHT_SIDE 			1
#define TOP_SIDE 			2
#define BOTTOM_SIDE 		3
#define TOP_LEFT_CORNER 	4
#define TOP_RIGHT_CORNER 	5
#define BOTTOM_LEFT_CORNER 	6
#define BOTTOM_RIGHT_CORNER 7

#define COLOR_LIGHT_BLUE 	8
#define COLOR_LIGHT_PURPLE 	9
#define COLOR_LIGHT_PINK 	10
#define COLOR_LIGHT_GREEN	11
#define COLOR_GRAY			12

#define WHITE_PAIR 		1
#define CTRLS_PAIR 		2
#define SELECT_ID_PAIR 	3
#define INFO_PAIR 		4
#define FOCUSED_PAIR	5
#define UNFOCUSED_PAIR	6

#define FOCUSED_WCHAR L"◉"
#define UNFOCUSED_WCHAR L"◎"
#define ELIPSIS_WCHAR L"…"

typedef struct {
	const char *name;
	const char *sshId;
} AgentId;

typedef struct {
	size_t noIds;
	size_t maxIds;
	size_t maxNameLen;
	size_t maxSshIdLen;
	size_t selected;
	unsigned char modified;
	AgentId **agentIds;
} AgentIdCollection;

const char *sshIgnoreMapSize[SSH_IGNORE_MAP_SIZE] = {
	"agent",
	"authorized_keys",
	"known_hosts",
	"known_hosts.old"
};

typedef enum wstyle { ROUNDED, DECORATED_ROUNDED } BORDER_STYLE;
const wchar_t *borderCharacters[][8] = {
	{ L"│", L"│", L"─", L"─", L"╭", L"╮", L"╰", L"╯" },
	{ L"│", L"│", L"═", L"═", L"╭", L"╮", L"╰", L"╯" },
	NULL
};

typedef enum title { CTRLS_TITLE, SELECT_ID_TITLE, INFO_TITLE } WINDOW_TITLE;
wchar_t *windowTitles[] = {
	L"🎮Controls",
	L"⚉ Select ID",
	L"🛈 Info",
	NULL
};

AgentIdCollection *genAgentIdColl();
AgentId *searchAgentId(AgentIdCollection *coll, const char *sshId);
void updateMaxSizes(const char *newName, const char *newSshId, AgentIdCollection *coll);
unsigned char addAgentId(const char *name, const char *sshId, AgentIdCollection *coll);
unsigned char editAgentId(const char *oldSshId, const char *newName, const char *newSshId, AgentIdCollection *coll);
void freeAgentIdColl(FILE *cfgFile, AgentIdCollection *coll);

void makeIfMissing(const char *path, int type);
char *getHomePath(size_t *outputLen);

char *getCfgPath();
unsigned char populateConfig(FILE *cfgFile, AgentIdCollection *coll);
unsigned char parseConfig(FILE *cfgFile, AgentIdCollection *coll);

int min(int a, int b);

size_t titlesMaxWidth();

void initColorPairs();
void addPredBorder(WINDOW *win, BORDER_STYLE border, short colorPair);
void addWTitle(WINDOW *win, wchar_t *title, short colorPair);
void printWrappedStr(WINDOW *win, char *str, short colorPair);
void printIds(WINDOW *win, AgentIdCollection *coll);

void startCurses();
void endCurses();

int main() {
	startCurses();
	char *cfgPath = getCfgPath();
	size_t cfgPathLen = strlen(cfgPath);
	FILE *cfgFile = fopen(cfgPath, "a+");
	
	AgentIdCollection *idColl = genAgentIdColl();
	char *infoMsg = (char *)calloc(1, 1024);

	int key;
	int rows, cols;
	size_t minCols, minRows;
	size_t ctrlsMsgRows, infoMsgRows;
	size_t containerCols;
	size_t fitRows;
	WINDOW *container;
	WINDOW *ctrlsWin, *infoWin, *selIdWin;

	if (fseek(cfgFile, 0, SEEK_END) == 0 && ftell(cfgFile) == 0) {
		populateConfig(cfgFile, idColl);
		strncpy(infoMsg, "Populated config file with default values at: ", 47);
	}
	rewind(cfgFile);

	if (idColl->noIds == 0) {
		parseConfig(cfgFile, idColl);
		strncpy(infoMsg, "Loaded found ssh ids at: ", 26);
	}
	strncat(infoMsg, cfgPath, cfgPathLen);

	const char *ctrlsMsg = "Move UP/DOWN, select ENTER, quit Q";
	minCols = titlesMaxWidth() + ENTRY_PADDING;
	minRows = 2 + idColl->noIds; // Temp implementation assuming 2/3 agents (+2 for borders)

	do {
		clear();
		getmaxyx(stdscr, rows, cols);

		containerCols = cols / 3;

		ctrlsMsgRows = 2 + ceil((1.0f * strlen(ctrlsMsg)) / (containerCols-2));
		infoMsgRows = 2 + ceil((1.0f * strlen(infoMsg)) / (containerCols-2));

		fitRows = minRows + ctrlsMsgRows + infoMsgRows;

		if (rows < fitRows || containerCols < minCols) {
			printw("Not enough space.");
			continue;
		}

		container = subwin(
			stdscr,
			fitRows, containerCols,
			rows / 2 - fitRows / 2, cols / 2 - containerCols / 2
		);

		ctrlsWin = derwin(container, ctrlsMsgRows, containerCols, 0, 0);
		printWrappedStr(ctrlsWin, (char *) ctrlsMsg, WHITE_PAIR);
		addPredBorder(ctrlsWin, ROUNDED, CTRLS_PAIR);
		addWTitle(ctrlsWin, windowTitles[CTRLS_TITLE], CTRLS_PAIR);

		selIdWin = derwin(container, minRows, containerCols, ctrlsMsgRows, 0);
		printIds(selIdWin, idColl);
		addPredBorder(selIdWin, DECORATED_ROUNDED, SELECT_ID_PAIR);
		addWTitle(selIdWin, windowTitles[SELECT_ID_TITLE], SELECT_ID_PAIR);

		infoWin = derwin(container, infoMsgRows, containerCols, ctrlsMsgRows+minRows, 0);
		printWrappedStr(infoWin, infoMsg, WHITE_PAIR);
		addPredBorder(infoWin, ROUNDED, INFO_PAIR);
		addWTitle(infoWin, windowTitles[INFO_TITLE], INFO_PAIR);

		refresh();
	} while((key = getch()) != 'q');

	freeAgentIdColl(cfgFile, idColl);
	free(cfgPath);
	fclose(cfgFile);
	endCurses();
	return 0;
}

AgentIdCollection *genAgentIdColl() {
	AgentIdCollection *coll = (AgentIdCollection *)calloc(1, sizeof(AgentIdCollection));
	coll->noIds = coll->modified = coll->selected = 0;
	coll->maxNameLen = coll->maxSshIdLen = 0;
	coll->maxIds = DEFAULT_AG_ID_NO;
	coll->agentIds = (AgentId **)calloc(coll->maxIds, sizeof(AgentId *));
	return coll;
}

AgentId *searchAgentId(AgentIdCollection *coll, const char *sshId) {
	assert(strlen(sshId) <= ENTRY_ID_MAX);

	AgentId *target, *current;
	target = current = NULL;

	for (size_t i = 0; target == NULL && i < coll->noIds; i++) {
		current = coll->agentIds[i];
		if (strncmp(sshId, current->sshId, strlen(current->sshId)) == 0)
			target = current;
	}

	return target;
}

void updateMaxSizes(const char *newName, const char *newSshId, AgentIdCollection *coll) {
	size_t nameLen = strlen(newName);
	size_t sshIdLen = strlen(newSshId);

	if (nameLen > coll->maxNameLen) coll->maxNameLen = nameLen;
	if (sshIdLen > coll->maxSshIdLen) coll->maxSshIdLen = sshIdLen;
}

unsigned char editAgentId(const char *oldSshId, const char *newName, const char *newSshId, AgentIdCollection *coll) {
	AgentId *foundId;
	if ((foundId = searchAgentId(coll, oldSshId)) == NULL) return 1;
	assert(strlen(newName) <= ENTRY_ID_MAX && strlen(newSshId) <= ENTRY_ID_MAX);

	foundId->name = newName;
	foundId->sshId = newSshId;
	updateMaxSizes(newName, newSshId, coll);
	return 0;
}

unsigned char addAgentId(const char *name, const char *sshId, AgentIdCollection *coll) {
	if (searchAgentId(coll, name) != NULL) return 1;

	AgentId *newAgent = (AgentId *)calloc(1, sizeof(AgentId));
	size_t nameLen, sshIdLen;

	newAgent->name = name; nameLen = strlen(name);
	newAgent->sshId = sshId; sshIdLen = strlen(sshId);

	if (coll->noIds == coll->maxIds) {
		coll->agentIds = reallocarray(coll->agentIds, coll->noIds+1, sizeof(AgentId *));
		coll->maxIds = coll->noIds;
	}

	updateMaxSizes(name, sshId, coll);
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

int min(int a, int b) { return a < b ? a : b; }

size_t titlesMaxWidth() {
	size_t maxLen = wcslen(*windowTitles), currentLen;

	for (size_t i = 1; windowTitles[i] != NULL; i++) {
		currentLen = wcslen(windowTitles[i]);
		if (currentLen > maxLen) maxLen = currentLen;
	}

	return maxLen;
}

void initColorPairs() {
	init_color(COLOR_LIGHT_BLUE, RGB_TO_CURSES(32), RGB_TO_CURSES(230), RGB_TO_CURSES(252));
	init_color(COLOR_LIGHT_PURPLE, RGB_TO_CURSES(155), RGB_TO_CURSES(45), RGB_TO_CURSES(252));
	init_color(COLOR_LIGHT_PINK, RGB_TO_CURSES(238), RGB_TO_CURSES(45), RGB_TO_CURSES(252));
	init_color(COLOR_LIGHT_GREEN, RGB_TO_CURSES(113), RGB_TO_CURSES(252), RGB_TO_CURSES(85));
	init_color(COLOR_GRAY, RGB_TO_CURSES(55), RGB_TO_CURSES(56), RGB_TO_CURSES(55));

	init_pair(WHITE_PAIR, COLOR_WHITE, COLOR_BLACK);
	init_pair(CTRLS_PAIR, COLOR_LIGHT_GREEN, COLOR_BLACK);
	init_pair(SELECT_ID_PAIR, COLOR_LIGHT_PURPLE, COLOR_BLACK);
	init_pair(INFO_PAIR, COLOR_LIGHT_PINK, COLOR_BLACK);
	init_pair(FOCUSED_PAIR, COLOR_LIGHT_BLUE, COLOR_BLACK);
	init_pair(UNFOCUSED_PAIR, COLOR_GRAY, COLOR_BLACK);
}

void addPredBorder(WINDOW *win, BORDER_STYLE border, short colorPair) {
	cchar_t ls, rs, bs, ts, tl, tr, bl, br;
	setcchar(&ls, borderCharacters[border][LEFT_SIDE], WA_NORMAL, colorPair, NULL);
	setcchar(&rs, borderCharacters[border][RIGHT_SIDE], WA_NORMAL, colorPair, NULL);
	setcchar(&ts, borderCharacters[border][TOP_SIDE], WA_NORMAL, colorPair, NULL);
	setcchar(&bs, borderCharacters[border][BOTTOM_SIDE], WA_NORMAL, colorPair, NULL);
	setcchar(&tl, borderCharacters[border][TOP_LEFT_CORNER], WA_NORMAL, colorPair, NULL);
	setcchar(&tr, borderCharacters[border][TOP_RIGHT_CORNER], WA_NORMAL, colorPair, NULL);
	setcchar(&bl, borderCharacters[border][BOTTOM_LEFT_CORNER], WA_NORMAL, colorPair, NULL);
	setcchar(&br, borderCharacters[border][BOTTOM_RIGHT_CORNER], WA_NORMAL, colorPair, NULL);
	wborder_set(win, &ls, &rs, &ts, &bs, &tl, &tr, &bl, &br);
}

void addWTitle(WINDOW *win, wchar_t *title, short colorPair) {
	wattr_set(win, WA_NORMAL, colorPair, NULL);
	mvwaddwstr(win, 0, 1, title);
}

void printWrappedStr(WINDOW *win, char *str, short colorPair) {
	colorPair = colorPair == -1 ? WHITE_PAIR : colorPair;
	wattr_set(win, WA_NORMAL, colorPair, NULL);

	size_t availableCols;

	availableCols = getmaxx(win) - 2; // -2 to account for borders

	for (size_t i = 0; i < getmaxy(win); i++)
		mvwaddnstr(win, i+1, 1, str+i*availableCols, availableCols);
}

void printIds(WINDOW *win, AgentIdCollection *coll) {
	size_t availableCols = getmaxx(win);
	// -2 for the selection chars / Splits the max length of name and id in half
	size_t textMaxLen = (availableCols - ENTRY_PADDING - 2) / 2;
	AgentId *current;

	for (size_t i = 0; i < coll->noIds; i++) {
		current = coll->agentIds[i];
		wattr_set(win, WA_NORMAL, i == coll->selected ? FOCUSED_PAIR : UNFOCUSED_PAIR, NULL);
		mvwaddwstr(win, 1+i, 1, i == coll->selected ? FOCUSED_WCHAR : UNFOCUSED_WCHAR);
		mvwaddnstr(win, 1+i, 3, current->name, textMaxLen-1);
		if (strlen(current->name) > textMaxLen-1) waddwstr(win, ELIPSIS_WCHAR);
	}
}

void startCurses() {
	setlocale(LC_ALL, "en_US.UTF-8");
	initscr();
	cbreak(); noecho(); keypad(stdscr, true);
	start_color();
	initColorPairs();
	curs_set(0);
}

void endCurses() {
	endwin();
}
