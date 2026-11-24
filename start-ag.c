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
#define INVERTED_PAIR	7

#define FOCUSED_WCHAR L"◉"
#define UNFOCUSED_WCHAR L"◎"
#define ELIPSIS_WCHAR L"…"
#define DOC_WCHAR L"🗎"

#define PAGE_SIZE 2

#define ENTER_KEY 10

typedef struct {
	char *name;
	char *sshId;
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
};

typedef enum title { CTRLS_TITLE, SELECT_ID_TITLE, INFO_TITLE } WINDOW_TITLE;
wchar_t *windowTitles[] = {
	L"🎮Controls",
	L"⚉ Select ID",
	L"🛈 Info",
	(wchar_t *) NULL
};

size_t currentPage = 0, totalPages = 0;

AgentIdCollection *genAgentIdColl();
AgentId *searchAgentId(AgentIdCollection *coll, const char *sshId);
void updateMaxSizes(const char *newName, const char *newSshId, AgentIdCollection *coll);
unsigned char addAgentId(const char *name, const char *sshId, AgentIdCollection *coll);
unsigned char editAgentId(const char *oldSshId, const char *newName, const char *newSshId, AgentIdCollection *coll);

void freeAgentId(AgentId *agentId);
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

void addSshId(AgentId *agentId);

void startCurses();
void endCurses();

int main() {
	startCurses();
	char *cfgPath = getCfgPath();
	size_t cfgPathLen = strlen(cfgPath);
	FILE *cfgFile = fopen(cfgPath, "a+");
	
	AgentIdCollection *idColl = genAgentIdColl();
	char *infoMsg = (char *)calloc(1, 1024);

	int rows, cols;
	size_t minCols, minRows;
	size_t ctrlsMsgRows, infoMsgRows;
	size_t containerCols;
	size_t fitRows;
	WINDOW *container;
	WINDOW *ctrlsWin, *selIdWin, *infoWin;

	char *newNameBuff, *newSshIdBuff;

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

	int key = 0;
	const char *ctrlsMsg = "Move ARROW KEYS; select ENTER; quit Q; A new agent; E edit agent";
	totalPages = ceil((1.0f * idColl->noIds) / PAGE_SIZE);
	minCols = titlesMaxWidth() + ENTRY_PADDING;
	minRows = 2 + PAGE_SIZE; // Temp implementation assuming 2/3 agents (+2 for borders)

	do {
		clear();
		getmaxyx(stdscr, rows, cols);

		if (key == KEY_DOWN) idColl->selected++;
		if (key == KEY_UP) idColl->selected--;
		if (key == KEY_RIGHT) currentPage++;
		if (key == KEY_LEFT) currentPage--;
		idColl->selected %= PAGE_SIZE;
		currentPage %= totalPages;

		if (key == ENTER_KEY) {
			addSshId(idColl->agentIds[idColl->selected*PAGE_SIZE]);
			break;
		}

		// The default is going to be ed25519, the best way to implement
		// this is through a form with a menu.
		if (key == 'a') {
			attr_set(WA_NORMAL, INVERTED_PAIR, NULL);
			mvprintw(rows-1, 0, "Name of the new identity: ");
			attr_set(WA_NORMAL, WHITE_PAIR, NULL);
			addch(' ');

			newNameBuff = (char *)calloc(1, ENTRY_ID_MAX);
			echo();
			curs_set(1);
			getstr(newNameBuff);
			noecho();
			curs_set(0);
			
			move(rows-1, 0);
			clrtoeol();
			attr_set(WA_NORMAL, INVERTED_PAIR, NULL);
			mvprintw(rows-1, 0, "SshId of the new identity: ");
			attr_set(WA_NORMAL, WHITE_PAIR, NULL);
			addch(' ');

			newSshIdBuff = (char *)calloc(1, ENTRY_ID_MAX);
			echo();
			curs_set(1);
			getstr(newSshIdBuff);
			noecho();
			curs_set(0);

			addAgentId(newNameBuff, newSshIdBuff, idColl);

			free(newNameBuff); free(newSshIdBuff);
			totalPages = ceil((1.0f * idColl->noIds) / PAGE_SIZE);
		}

		if (key == 'e') {

		}

		containerCols = cols / 3;

		ctrlsMsgRows = 2 + ceil((1.0f * strlen(ctrlsMsg)) / (containerCols-2));
		infoMsgRows = 2 + ceil((1.0f * strlen(infoMsg)) / (containerCols-2));

		fitRows = minRows + ctrlsMsgRows + infoMsgRows;

		if (rows < (int) fitRows || containerCols < minCols) {
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
		if (idColl->noIds > PAGE_SIZE) {
			mvwaddwstr(selIdWin, minRows-1, 1, DOC_WCHAR);
			mvwprintw(selIdWin, minRows-1, 2, "  %zu/%zu", currentPage+1, totalPages);
		}

		infoWin = derwin(container, infoMsgRows, containerCols, ctrlsMsgRows+minRows, 0);
		printWrappedStr(infoWin, infoMsg, WHITE_PAIR);
		addPredBorder(infoWin, ROUNDED, INFO_PAIR);
		addWTitle(infoWin, windowTitles[INFO_TITLE], INFO_PAIR);

		refresh();
	} while((key = getch()) != 'q');

	freeAgentIdColl(cfgFile, idColl);
	free(cfgPath);
	free(infoMsg);
	fclose(cfgFile);
	endCurses();
	return 0;
}

AgentIdCollection *genAgentIdColl() {
	AgentIdCollection *coll = (AgentIdCollection *)calloc(1, sizeof(AgentIdCollection));
	coll->noIds = coll->modified =  coll->selected = 0;
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

	size_t newNameLen, newSshIdLen;

	newNameLen = strlen(newName); newSshIdLen = strlen(newSshId);

	free(foundId->name); free(foundId->sshId);
	foundId->name = (char *)calloc(1, newNameLen+1);
	foundId->sshId = (char *)calloc(1, newSshIdLen+1);
	strncpy(foundId->name, newName, newNameLen);
	strncpy(foundId->sshId, newSshId, newSshIdLen);

	updateMaxSizes(newName, newSshId, coll);
	return 0;
}

unsigned char addAgentId(const char *name, const char *sshId, AgentIdCollection *coll) {
	if (searchAgentId(coll, name) != NULL) return 1;

	AgentId *newAgent = (AgentId *)calloc(1, sizeof(AgentId));
	size_t nameLen, sshIdLen;

	nameLen = strlen(name); sshIdLen = strlen(sshId);
	// +1 to account for \0
	newAgent->name = (char *)calloc(1, nameLen+1);
	newAgent->sshId = (char *)calloc(1, sshIdLen+1);

	strncpy(newAgent->name, name, nameLen);
	strncpy(newAgent->sshId, sshId, nameLen);

	if (coll->noIds == coll->maxIds) {
		coll->agentIds = reallocarray(coll->agentIds, coll->noIds+1, sizeof(AgentId *));
		coll->maxIds = coll->noIds;
	}

	updateMaxSizes(name, sshId, coll);
	coll->agentIds[coll->noIds++] = newAgent;
	return 0;
}

void freeAgentId(AgentId *agentId) {
	free(agentId->name);
	free(agentId->sshId);
	free(agentId);
}

void freeAgentIdColl(FILE *cfgFile, AgentIdCollection *coll) {
	AgentId *current;
	unsigned char mod = coll->modified;
	if (mod) cfgFile = freopen(NULL, "w", cfgFile);

	for (size_t i = 0; i < coll->noIds; i++) {
		current = coll->agentIds[i];
		if (mod) fprintf(cfgFile, ENTRY_FORMAT_STR, current->name, current->sshId);
		freeAgentId(current);
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
	init_color(COLOR_LIGHT_GREEN, RGB_TO_CURSES(113), RGB_TO_CURSES(252), RGB_TO_CURSES(115));
	init_color(COLOR_GRAY, RGB_TO_CURSES(74), RGB_TO_CURSES(76), RGB_TO_CURSES(74));

	init_pair(WHITE_PAIR, COLOR_WHITE, COLOR_BLACK);
	init_pair(CTRLS_PAIR, COLOR_LIGHT_GREEN, COLOR_BLACK);
	init_pair(SELECT_ID_PAIR, COLOR_LIGHT_PURPLE, COLOR_BLACK);
	init_pair(INFO_PAIR, COLOR_LIGHT_PINK, COLOR_BLACK);
	init_pair(FOCUSED_PAIR, COLOR_LIGHT_BLUE, COLOR_BLACK);
	init_pair(UNFOCUSED_PAIR, COLOR_GRAY, COLOR_BLACK);
	init_pair(INVERTED_PAIR, COLOR_BLACK, COLOR_WHITE);
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

	for (size_t i = 0; (int) i < getmaxy(win)-2; i++)
		mvwaddnstr(win, i+1, 1, str+i*availableCols, availableCols);
}

void printIds(WINDOW *win, AgentIdCollection *coll) {
	size_t availableCols = getmaxx(win)-2;
	// -2 for the selection chars / Splits the max length of name and id in half
	size_t textMaxLen = (availableCols - ENTRY_PADDING - 2) / 2;
	size_t currentNameLen, currentSshIdLen;
	AgentId *current;

	for (size_t i = 0; (int) i < min(PAGE_SIZE, coll->noIds-currentPage*PAGE_SIZE); i++) {
		current = coll->agentIds[i+currentPage*PAGE_SIZE];
		currentNameLen = strlen(current->name);
		currentSshIdLen = strlen(current->sshId);
		wattr_set(win, WA_NORMAL, i == coll->selected ? FOCUSED_PAIR : UNFOCUSED_PAIR, NULL);
		mvwaddwstr(win, 1+i, 1, i == coll->selected ? FOCUSED_WCHAR : UNFOCUSED_WCHAR);

		mvwaddnstr(win, 1+i, 3, current->name, textMaxLen-1);
		if (currentNameLen > textMaxLen-1) waddwstr(win, ELIPSIS_WCHAR);

		mvwaddnstr(win, 1+i, availableCols-min(textMaxLen, currentSshIdLen)+1, current->sshId, textMaxLen-1);
		if (currentSshIdLen > textMaxLen-1) waddwstr(win, ELIPSIS_WCHAR);
	}
}

void addSshId(AgentId *agentId) {
	const char *addCmdFormatStr = "ssh-add %s.ssh/%s";
	size_t sshIdLen = strlen(agentId->sshId);

	size_t homePathLen;
	char *homePath = getHomePath(&homePathLen);
	char *addCmdBuff = (char *)calloc(1, strlen(addCmdFormatStr) - 4 + homePathLen + sshIdLen);

	sprintf(addCmdBuff, addCmdFormatStr, homePath, agentId->sshId);
	system(addCmdBuff);

	free(addCmdBuff);
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
