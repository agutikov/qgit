/*
	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution

*/
#ifndef GIT_H
#define GIT_H

#include <QAbstractItemModel>
#include "exceptionmanager.h"
#include "common.h"

template <class, class> struct QPair;
class QRegExp;
class QTextCodec;
class Annotate;
class Cache;
class DataLoader;
class Domain;
class Git;
class Lanes;
class MyProcess;

class FileHistory : public QAbstractItemModel {
Q_OBJECT
public:
	FileHistory(QObject* parent, Git* git);
	~FileHistory();
	void clear();
	const QString sha(int row) const;
	int row(SCRef sha) const;
	const QStringList fileNames() const { return fNames; }
	void resetFileNames(SCRef fn);
	void setAnnIdValid(bool b = true) { _annIdValid = b; }

	virtual QVariant data(const QModelIndex &index, int role) const;
	virtual Qt::ItemFlags flags(const QModelIndex& index) const;
	virtual QVariant headerData(int s, Qt::Orientation o, int role = Qt::DisplayRole) const;
	virtual QModelIndex index(int r, int c, const QModelIndex& par = QModelIndex()) const;
	virtual QModelIndex parent(const QModelIndex& index) const;
	virtual int rowCount(const QModelIndex& par = QModelIndex()) const;
	virtual bool hasChildren(const QModelIndex& par = QModelIndex()) const;
	virtual int columnCount(const QModelIndex&) const { return 5; }

private slots:
	void on_newRevsAdded(const FileHistory*, const QVector<QString>&);
	void on_loadCompleted(const FileHistory*, const QString&);

private:
	friend class Annotate;
	friend class DataLoader;
	friend class Git;

	const QString timeDiff(unsigned long secs) const;

	Git* git;
	RevMap revs;
	StrVect revOrder;
	Lanes* lns;
	uint firstFreeLane;
	QList<QByteArray*> rowData;
	QList<QVariant> _headerInfo;
	int _rowCnt;
	bool _annIdValid;
	unsigned long _secs;
	int loadTime;
	QStringList fNames;
	QStringList curFNames;
	QStringList renamedRevs;
	QHash<QString, QString> renamedPatches;
};

struct Reference { // stores tag information associated to a revision
	Reference() : type(0) {}
	uint type;
	QStringList branches;
	QStringList remoteBranches;
	QString     currentBranch;
	QStringList tags;
	QStringList refs;
	QString     tagObj; // TODO support more then one obj
	QString     tagMsg;
	QString     stgitPatch;
};
typedef QHash<QString, Reference> RefMap;
SHA_HASH_DECL(Reference);


class Git : public QObject {
Q_OBJECT
public:
	explicit Git(QObject* parent);

	enum BoolOption { // used as self-documenting boolean parameters
		optFalse,
		optSaveCache,
		optGoDown,
		optOnlyLoaded,
		optDragDrop,
		optFold,
		optOnlyInIndex,
		optCreate
	};

	enum RefType {
		TAG        = 1,
		BRANCH     = 2,
		RMT_BRANCH = 4,
		CUR_BRANCH = 8,
		REF        = 16,
		APPLIED    = 32,
		UN_APPLIED = 64,
		ANY_REF    = 127
	};

	struct TreeEntry {
		TreeEntry(SCRef n, SCRef s, SCRef t) : name(n), sha(s), type(t) {}
		bool operator<(const TreeEntry&) const;
		QString name;
		QString sha;
		QString type;
	};
	typedef QList<TreeEntry> TreeInfo;

	void setDefaultModel(FileHistory* fh) { revData = fh; }
	void checkEnvironment();
	void userInfo(SList info);
	const QString getBaseDir(bool* c, SCRef wd, bool* ok = NULL, QString* gd = NULL);
	bool init(SCRef workDir, bool askForRange, QStringList* filterList, bool* quit);
	void stop(bool saveCache);
	void setThrowOnStop(bool b);
	bool isThrowOnStopRaised(int excpId, SCRef curContext);
	void setLane(SCRef sha, FileHistory* fh);
	Annotate* startAnnotate(FileHistory* fh, QObject* guiObj);
	const FileAnnotation* lookupAnnotation(Annotate* ann, SCRef sha);
	void cancelAnnotate(Annotate* ann);
	bool startFileHistory(SCRef sha, SCRef startingFileName, FileHistory* fh);
	void cancelDataLoading(const FileHistory* fh);
	void cancelProcess(MyProcess* p);
	bool isCommittingMerge() const { return isMergeHead; }
	bool isStGITStack() const { return isStGIT; }
	bool isPatchName(SCRef nm);
	bool isSameFiles(SCRef tree1Sha, SCRef tree2Sha);
	static bool isImageFile(SCRef file);
	static bool isBinaryFile(SCRef file);
	bool isNothingToCommit();
	bool isUnknownFiles() const { return (_wd.otherFiles.count() > 0); }
	bool isTextHighlighter() const { return isTextHighlighterFound; }
	bool isMainHistory(const FileHistory* fh) { return (fh == revData); }
	MyProcess* getDiff(SCRef sha, QObject* receiver, SCRef diffToSha, bool combined);
	const QString getWorkDirDiff(SCRef fileName = "");
	MyProcess* getFile(SCRef fileSha, QObject* receiver, QByteArray* result, SCRef fileName);
	MyProcess* getHighlightedFile(SCRef fileSha, QObject* receiver, QString* result, SCRef fileName);
	const QString getFileSha(SCRef file, SCRef revSha);
	bool saveFile(SCRef fileSha, SCRef fileName, SCRef path);
	void getFileFilter(SCRef path, ShaSet& shaSet);
	bool getPatchFilter(SCRef exp, bool isRegExp, ShaSet& shaSet);
	const RevFile* getFiles(SCRef sha, SCRef sha2 = "", bool all = false, SCRef path = "");
	bool getTree(SCRef ts, TreeInfo& ti, bool wd, SCRef treePath);
	static const QString getLocalDate(SCRef gitDate);
	const QString getDesc(SCRef sha, QRegExp& slogRE, QRegExp& lLogRE, bool showH, FileHistory* fh);
	const QString getDefCommitMsg();
	const QString getLaneParent(SCRef fromSHA, int laneNum);
	const QStringList getChilds(SCRef parent);
	const QStringList getNearTags(bool goDown, SCRef sha);
	const QStringList getDescendantBranches(SCRef sha, bool shaOnly = false);
	const QString getShortLog(SCRef sha);
	const QString getTagMsg(SCRef sha);
	const Rev* revLookup(SCRef sha, const FileHistory* fh = NULL) const;
	uint checkRef(SCRef sha, uint mask = ANY_REF) const;
	const QString getRevInfo(SCRef sha);
	const QString getRefSha(SCRef refName, RefType type = ANY_REF, bool askGit = true);
	const QStringList getRefName(SCRef sha, RefType type, QString* curBranch = NULL) const;
	const QStringList getAllRefNames(uint mask, bool onlyLoaded);
	const QStringList getAllRefSha(uint mask);
	void getWorkDirFiles(SList files, SList dirs, RevFile::StatusFlag status);
	QTextCodec* getTextCodec(bool* isGitArchive);
	bool formatPatch(SCList shaList, SCRef dirPath, SCRef remoteDir = "");
	bool updateIndex(SCList selFiles);
	bool commitFiles(SCList files, SCRef msg);
	bool makeTag(SCRef sha, SCRef tag, SCRef msg);
	bool deleteTag(SCRef sha);
	bool applyPatchFile(SCRef patchPath, bool fold, bool sign);
	bool resetCommits(int parentDepth);
	bool stgCommit(SCList selFiles, SCRef msg, SCRef patchName, bool fold);
	bool stgPush(SCRef sha);
	bool stgPop(SCRef sha);
	void setTextCodec(QTextCodec* tc);
	void addExtraFileInfo(QString* rowName, SCRef sha, SCRef diffToSha, bool allMergeFiles);
	void removeExtraFileInfo(QString* rowName);
	void formatPatchFileHeader(QString* rowName, SCRef sha, SCRef dts, bool cmb, bool all);
	int findFileIndex(const RevFile& rf, SCRef name);
	const QString filePath(const RevFile& rf, uint i) const {

		return dirNamesVec[rf.dirs[i]] + fileNamesVec[rf.names[i]];
	}
	void setCurContext(Domain* d) { curDomain = d; }
	Domain* curContext() const { return curDomain; }

signals:
	void newRevsAdded(const FileHistory*, const QVector<QString>&);
	void loadCompleted(const FileHistory*, const QString&);
	void cancelLoading(const FileHistory*);
	void cancelAllProcesses();
	void annotateReady(Annotate*, bool, const QString&);

public slots:
	void procReadyRead(const QByteArray&);
	void procFinished() { filesLoadingPending = filesLoadingCurSha = ""; }

private slots:
	void loadFileNames();
	void on_runAsScript_eof();
	void on_getHighlightedFile_eof();
	void on_newDataReady(const FileHistory*);
	void on_loaded(FileHistory*, ulong,int,bool,const QString&,const QString&);

private:
	friend class MainImpl;
	friend class DataLoader;
	friend class ConsoleImpl;
	friend class RevsView;

	struct WorkingDirInfo {
		void clear() { diffIndex = diffIndexCached = ""; otherFiles.clear(); }
		QString diffIndex;
		QString diffIndexCached;
		QStringList otherFiles;
	};
	WorkingDirInfo _wd;

	struct StartParmeters { // used to pass arguments to init2()
		QStringList args;
		bool filteredLoading;
	};
	StartParmeters _sp;

	void init2();
	bool run(SCRef cmd, QString* out = NULL, QObject* rcv = NULL, SCRef buf = "");
	bool run(QByteArray* runOutput, SCRef cmd, QObject* rcv = NULL, SCRef buf = "");
	MyProcess* runAsync(SCRef cmd, QObject* rcv, SCRef buf = "");
	MyProcess* runAsScript(SCRef cmd, QObject* rcv = NULL, SCRef buf = "");
	const QStringList getArgs(bool askForRange, bool* quit);
	bool getRefs();
	void parseStGitPatches(SCList patchNames, SCList patchShas);
	void clearRevs();
	void clearFileNames();
	bool startRevList(SCList args, FileHistory* fh);
	bool startUnappliedList();
	bool startParseProc(SCList initCmd, FileHistory* fh, SCRef buf);
	bool tryFollowRenames(FileHistory* fh);
	bool populateRenamedPatches(SCRef sha, SCList nn, FileHistory* fh, QStringList* on, bool bt);
	int addChunk(FileHistory* fh, const QByteArray& ba, int ofs);
	void parseDiffFormat(RevFile& rf, SCRef buf);
	void parseDiffFormatLine(RevFile& rf, SCRef line, int parNum);
	void getDiffIndex();
	Rev* fakeRevData(SCRef sha, SCList parents, SCRef author, SCRef date, SCRef log,
                         SCRef longLog, SCRef patch, int idx, FileHistory* fh);
	const Rev* fakeWorkDirRev(SCRef parent, SCRef log, SCRef longLog, int idx, FileHistory* fh);
	const RevFile* fakeWorkDirRevFile(const WorkingDirInfo& wd);
	bool copyDiffIndex(FileHistory* fh, SCRef parent);
	const RevFile* insertNewFiles(SCRef sha, SCRef data);
	const RevFile* getAllMergeFiles(const Rev* r);
	bool isParentOf(SCRef par, SCRef child);
	bool isTreeModified(SCRef sha);
	void indexTree();
	void updateDescMap(const Rev* r, uint i, QHash<QPair<uint, uint>,bool>& dm,
	                   QHash<uint, QVector<int> >& dv);
	void mergeNearTags(bool down, Rev* p, const Rev* r, const QHash<QPair<uint, uint>, bool>&dm);
	void mergeBranches(Rev* p, const Rev* r);
	void updateLanes(Rev& c, Lanes& lns, SCRef sha);
	static void removeFiles(SCList selFiles, SCRef workDir, SCRef ext);
	static void restoreFiles(SCList selFiles, SCRef workDir, SCRef ext);
	bool mkPatchFromIndex(SCRef msg, SCRef patchFile);
	const QStringList getOthersFiles();
	const QStringList getOtherFiles(SCList selFiles, bool onlyInIndex);
	const QString getNewestFileName(SCList args, SCRef fileName);
	static const QString colorMatch(SCRef txt, QRegExp& regExp);
	void appendFileName(RevFile& rf, SCRef name);
	void populateFileNamesMap();
	const QString formatList(SCList sl, SCRef name, bool inOneLine = true);
	static const QString quote(SCRef nm);
	static const QString quote(SCList sl);
	static const QStringList noSpaceSepHack(SCRef cmd);
	void removeDeleted(SCList selFiles);
	void setStatus(RevFile& rf, SCRef rowSt);
	void setExtStatus(RevFile& rf, SCRef rowSt, int parNum);
	void appendNamesWithId(QStringList& names, SCRef sha, SCList data, bool onlyLoaded);
	Reference* lookupReference(SCRef sha, bool create = false);

	EM_DECLARE(exGitStopped);

	Domain* curDomain;
	QString workDir; // workDir is always without trailing '/'
	QString gitDir;
	QString filesLoadingPending;
	QString filesLoadingCurSha;
	QString curRange;
	bool cacheNeedsUpdate;
	bool errorReportingEnabled;
	bool isMergeHead;
	bool isStGIT;
	bool isGIT;
	bool isTextHighlighterFound;
	bool loadingUnAppliedPatches;
	bool fileCacheAccessed;
	int patchesStillToFind;
	QString firstNonStGitPatch;
	RevFileMap revsFiles;
	RefMap refsShaMap;
	StrVect fileNamesVec;
	StrVect dirNamesVec;
	QHash<QString, int> fileNamesMap; // quick lookup file name
	QHash<QString, int> dirNamesMap;  // quick lookup directory name
	FileHistory* revData;
};

#endif
