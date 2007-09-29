/*
	Description: interface to sync and async external program execution

	Author: Marco Costalba (C) 2005-2007

	Copyright: See COPYING file that comes with this distribution

*/
#include <QApplication>
#include "exceptionmanager.h"
#include "common.h"
#include "domain.h"
#include "myprocess.h"

MyProcess::MyProcess(QObject *go, Git* g, const QString& wd, bool err) : QProcess(g) {

	guiObject = go;
	git = g;
	workDir = wd;
	runOutput = NULL;
	receiver = NULL;
	errorReportingEnabled = err;
	canceling = async = isWinShell = isErrorExit = false;
}

bool MyProcess::runAsync(SCRef rc, QObject* rcv, SCRef buf) {

	async = true;
	runCmd = rc;
	receiver = rcv;
	setupSignals();
	if (!launchMe(runCmd, buf))
		return false; // caller will delete us

	return true;
}

bool MyProcess::runSync(SCRef rc, QByteArray* ro, QObject* rcv, SCRef buf) {

	async = false;
	runCmd = rc;
	runOutput = ro;
	receiver = rcv;
	if (runOutput)
		runOutput->clear();

	setupSignals();
	if (!launchMe(runCmd, buf))
		return false;

	busy = true; // we have to wait here until we exit

	// we are now leaving our safe domain context and
	// jump right in to processEvents() hyperspace
	Domain* d = git->curContext();
	git->setCurContext(NULL);

	EM_BEFORE_PROCESS_EVENTS;

	while (busy) {
		waitForFinished(20); // suspend 20ms to let OS reschedule
		qApp->processEvents();
	}

	EM_AFTER_PROCESS_EVENTS;

	if (git->curContext())
		qDebug("ASSERT in MyProcess::runSync, context is %p "
		       "instead of NULL", (void*)git->curContext());

	git->setCurContext(d); // restore our context

	return !isErrorExit;
}

void MyProcess::setupSignals() {

	connect(git, SIGNAL(cancelAllProcesses()),
	        this, SLOT(on_cancel()));

	connect(this, SIGNAL(readyReadStandardOutput()),
	        this, SLOT(on_readyReadStandardOutput()));

	connect(this, SIGNAL(finished(int, QProcess::ExitStatus)),
	        this, SLOT(on_finished(int, QProcess::ExitStatus)));

	if (receiver) {

		connect(this, SIGNAL(readyReadStandardError ()),
		        this, SLOT(on_readyReadStandardError()));

		connect(this, SIGNAL(procDataReady(const QByteArray&)),
		        receiver, SLOT(procReadyRead(const QByteArray&)));

		connect(this, SIGNAL(eof()), receiver, SLOT(procFinished()));
	}
	Domain* d = git->curContext();
	if (d)
		connect(d, SIGNAL(cancelDomainProcesses()), this, SLOT(on_cancel()));
}

void MyProcess::sendErrorMsg(bool notStarted, SCRef err) {

	if (!errorReportingEnabled)
		return;

	QString errorDesc(readAllStandardError());
	errorDesc.prepend(err);

	if (notStarted)
		errorDesc = QString::fromAscii("Unable to start the process!");

	const QString cmd(arguments.join(" ")); // hide any QUOTE_CHAR or related stuff
	MainExecErrorEvent* e = new MainExecErrorEvent(cmd, errorDesc);
	QApplication::postEvent(guiObject, e);
}

bool MyProcess::launchMe(SCRef runCmd, SCRef buf) {

	arguments = splitArgList(runCmd);
	if (arguments.isEmpty())
		return false;

	setWorkingDirectory(workDir);
	if (!QGit::startProcess(this, arguments, buf, &isWinShell)) {
		sendErrorMsg(true);
		return false;
	}
	return true;
}

void MyProcess::on_readyReadStandardOutput() {

	if (canceling)
		return;

	if (receiver)
		emit procDataReady(readAllStandardOutput());

	else if (runOutput)
		runOutput->append(readAllStandardOutput());
}

void MyProcess::on_readyReadStandardError() {

	if (canceling)
		return;

	if (receiver)
		emit procDataReady(readAllStandardError()); // redirect to stdout
	else
		dbs("ASSERT in myReadFromStderr: NULL receiver");
}

void MyProcess::on_finished(int exitCode, QProcess::ExitStatus exitStatus) {

	// Checking exingStatus is not reliable under Windows where if the
	// process was terminated with TerminateProcess() from another
	// application its value is still NormalExit
	//
	// Checking exit code for a failing command is unreliable too, as
	// exmple 'git status' returns -1 also without errors, and
	// 'git repo-config' returns 1 in normal case.
	//
	// On Windows exit code seems reliable in case of a command wrapped
	// in Window shell interpreter.
	//
	// So to detect a failing command we check also if stderr is not empty.
	QString errorDesc(readAllStandardError());

	isErrorExit =   (exitStatus != QProcess::NormalExit)
	             || (exitCode != 0 && isWinShell)
	             || !errorDesc.isEmpty()
	             ||  canceling;

	if (!canceling) { // no more noise after cancel

		if (receiver)
			emit eof();

		if (isErrorExit)
			sendErrorMsg(false, errorDesc);
	}
	busy = false;
	if (async)
		deleteLater();
}

void MyProcess::on_cancel() {

	canceling = true;

#ifdef Q_OS_WIN32
	kill(); // uses TerminateProcess
#else
	terminate(); // uses SIGTERM signal
#endif
}

const QStringList MyProcess::splitArgList(SCRef cmd) {
// return argument list handling quotes and double quotes
// substring, as example from:
// cmd some_arg "some thing" v='some value'
// to (comma separated fields)
// sl = <cmd,some_arg,some thing,v='some value'>

	// early exit the common case
	if (!(   cmd.contains(QGit::QUOTE_CHAR)
	      || cmd.contains("\"")
	      || cmd.contains("\'")))
		return cmd.split(' ', QString::SkipEmptyParts);

	// we have some work to do...
	// first find a possible separator
	const QString sepList("#%&!?"); // separator candidates
	int i = 0;
	while (cmd.contains(sepList[i]) && i < sepList.length())
		i++;

	if (i == sepList.length()) {
		dbs("ASSERT no unique separator found.");
		return QStringList();
	}
	const QChar& sepChar(sepList[i]);

	// remove all spaces
	QString newCmd(cmd);
	newCmd.replace(QChar(' '), sepChar);

	// re-add spaces in quoted sections
	restoreSpaces(newCmd, sepChar);

	// QUOTE_CHAR is used internally to delimit arguments
	// with quoted text wholly inside as
	// arg1 = <[patch] cool patch on "cool feature">
	// and should be removed before to feed QProcess
	newCmd.remove(QGit::QUOTE_CHAR);

	// QProcess::setArguments doesn't want quote
	// delimited arguments, so remove trailing quotes
	QStringList sl(newCmd.split(sepChar, QString::SkipEmptyParts));
	QStringList::iterator it(sl.begin());
	for ( ; it != sl.end(); ++it) {
		if (((*it).left(1) == "\"" && (*it).right(1) == "\"") ||
		   ((*it).left(1) == "\'" && (*it).right(1) == "\'"))
			*it = (*it).mid(1, (*it).length() - 2);
	}
	return sl;
}

void MyProcess::restoreSpaces(QString& newCmd, const QChar& sepChar) {
// restore spaces inside quoted text, supports nested quote types

	QChar quoteChar;
	bool replace = false;
	for (int i = 0; i < newCmd.length(); i++) {

		const QChar& c = newCmd[i];

		if (    !replace
		    && (c == QGit::QUOTE_CHAR[0] || c == '\"' || c == '\'')
		    && (newCmd.count(c) % 2 == 0)) {

				replace = true;
				quoteChar = c;
				continue;
		}
		if (replace && (c == quoteChar)) {
			replace = false;
			continue;
		}
		if (replace && c == sepChar)
			newCmd[i] = QChar(' ');
	}
}
