/******************************************************************************

  This source file is part of the Avogadro project.

  Copyright 2013 Kitware, Inc.

  This source code is released under the New BSD License, (the "License").

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

******************************************************************************/

#include "obprocess.h"

#include <QtCore/QDebug>
#include <QtCore/QFileInfo>
#include <QtCore/QProcess>
#include <QtCore/QRegExp>

namespace Avogadro {
namespace QtPlugins {

OBProcess::OBProcess(QObject *parent_) :
  QObject(parent_),
  m_processLocked(false),
  m_aborted(false),
  m_process(new QProcess(this)),
  m_obabelExecutable("obabel")
{
  // Read the AVO_OBABEL_EXECUTABLE env var to optionally override the
  // executable used for obabel.
  QByteArray obabelExec = qgetenv("AVO_OBABEL_EXECUTABLE");
  if (!obabelExec.isEmpty())
    m_obabelExecutable = obabelExec;
}

void OBProcess::abort()
{
  m_aborted = true;
  emit aborted();
}

bool OBProcess::queryReadFormats()
{
  if (!tryLockProcess()) {
    qWarning() << "OBProcess::queryReadFormats: process already in use.";
    return false;
  }

  // Setup options
  QStringList options;
  options << "-L" << "formats" << "read";

  executeObabel(options, this, SLOT(queryReadFormatsPrepare()));
  return true;
}

void OBProcess::queryReadFormatsPrepare()
{
  if (m_aborted) {
    releaseProcess();
    return;
  }

  QMap<QString, QString> result;

  QString output = QString::fromLatin1(m_process->readAllStandardOutput());

  QRegExp parser("\\s*([^\\s]+)\\s+--\\s+([^\\n]+)\\n");
  int pos = 0;
  while ((pos = parser.indexIn(output, pos)) != -1) {
    QString extension = parser.cap(1);
    QString description = parser.cap(2);
    result.insertMulti(description, extension);
    pos += parser.matchedLength();
  }

  releaseProcess();
  emit queryReadFormatsFinished(result);
  return;
}

bool OBProcess::readFile(const QString &filename,
                         const QString &outputFormat,
                         const QString &inputFormatOverride)
{
  if (!tryLockProcess()) {
    qWarning() << "OBProcess::readFile: process already in use.";
    return false;
  }

  QStringList options;
  QString inputFormat = inputFormatOverride.isEmpty() ?
                        QFileInfo(filename).suffix() : inputFormatOverride;

  // Setup input options
  options << QString("-i%1").arg(inputFormat);
  options << filename;

  // Setup output options
  options << QString("-o%1").arg(outputFormat);

  // See if we are using a format that never has 3D coordinates.
  QStringList specialFormats;
  specialFormats << "smi" << "inchi" << "can";
  if (specialFormats.contains(inputFormat))
    options << "--gen3d";

  executeObabel(options, this, SLOT(readFilePrepareOutput()));
  return true;
}

void OBProcess::readFilePrepareOutput()
{
  if (m_aborted) {
    releaseProcess();
    return;
  }

  // Keep this empty if an error occurs:
  QByteArray output;

  // Check for errors.
  QString errorOutput = QString::fromLatin1(m_process->readAllStandardError());
  QRegExp errorChecker("\\b0 molecules converted\\b" "|"
                       "obabel: cannot read input format!");
  if (!errorOutput.contains(errorChecker)) {
    if (m_process->exitStatus() == QProcess::NormalExit)
      output = m_process->readAllStandardOutput();
  }

  /// Print any meaningful warnings @todo This should go to a log at some point.
  if (!errorOutput.isEmpty() && errorOutput != "1 molecule converted\n")
    qDebug() << m_obabelExecutable << " stderr:\n" << errorOutput;

  emit readFileFinished(output);
  releaseProcess();
}

bool OBProcess::convert(const QByteArray &input, const QString &inFormat,
                        const QString &outFormat, const QStringList &options)
{
  if (!tryLockProcess()) {
    qWarning() << "OBProcess::convert: process already in use.";
    return false;
  }

  QStringList realOptions;
  realOptions << QString("-i%1").arg(inFormat)
              << QString("-o%1").arg(outFormat)
              << options;

  executeObabel(realOptions, this, SLOT(convertPrepareOutput()), input);
  return true;
}

void OBProcess::convertPrepareOutput()
{
  if (m_aborted) {
    releaseProcess();
    return;
  }

  // Keep this empty if an error occurs:
  QByteArray output;

  // Check for errors.
  QString errorOutput = QString::fromLatin1(m_process->readAllStandardError());
  QRegExp errorChecker("\\b0 molecules converted\\b" "|"
                       "obabel: cannot read input format!");
  if (!errorOutput.contains(errorChecker)) {
    if (m_process->exitStatus() == QProcess::NormalExit)
      output = m_process->readAllStandardOutput();
  }

  /// Print any meaningful warnings @todo This should go to a log at some point.
  if (!errorOutput.isEmpty() && errorOutput != "1 molecule converted\n")
    qDebug() << m_obabelExecutable << " stderr:\n" << errorOutput;

  emit convertFinished(output);
  releaseProcess();
}

bool OBProcess::queryForceFields()
{
  if (!tryLockProcess()) {
    qWarning() << "OBProcess::queryForceFields(): process already in use.";
    return false;
  }

  QStringList options;
  options << "-L" << "forcefields";

  executeObabel(options, this, SLOT(queryForceFieldsPrepare()));
  return true;
}

void OBProcess::queryForceFieldsPrepare()
{
  if (m_aborted) {
    releaseProcess();
    return;
  }

  QMap<QString, QString> result;

  QString output = QString::fromLatin1(m_process->readAllStandardOutput());

  QRegExp parser("([^\\s]+)\\s+(\\S[^\\n]*[^\\n\\.]+)\\.?\\n");
  int pos = 0;
  while ((pos = parser.indexIn(output, pos)) != -1) {
    QString key = parser.cap(1);
    QString desc = parser.cap(2);
    result.insertMulti(key, desc);
    pos += parser.matchedLength();
  }

  releaseProcess();
  emit queryForceFieldsFinished(result);
}

bool OBProcess::optimizeGeometry(const QByteArray &cml,
                                 const QStringList &options)
{
  if (!tryLockProcess()) {
    qWarning() << "OBProcess::optimizeGeometry(): process already in use.";
    return false;
  }

  QStringList realOptions;
  realOptions << "-icml" << "-ocml" << "--minimize" << options;

  // We'll need to read the log (printed to stderr) to update progress
  connect(m_process, SIGNAL(readyReadStandardError()),
          SLOT(optimizeGeometryReadLog()));

  // Initialize the log reader ivars
  m_optimizeGeometryLog.clear();
  m_optimizeGeometryMaxSteps = -1;

  // Start the optimization
  executeObabel(realOptions, this, SLOT(optimizeGeometryPrepare()), cml);
  return true;
}

void OBProcess::optimizeGeometryPrepare()
{
  if (m_aborted) {
    releaseProcess();
    return;
  }

  QByteArray result = m_process->readAllStandardOutput();

  releaseProcess();
  emit optimizeGeometryFinished(result);
}

void OBProcess::optimizeGeometryReadLog()
{
  // Append the current stderr to the log
  m_optimizeGeometryLog +=
      QString::fromLatin1(m_process->readAllStandardError());

  // Search for the maximum number of steps if we haven't found it yet
  if (m_optimizeGeometryMaxSteps < 0) {
    QRegExp maxStepsParser("\nSTEPS = ([0-9]+)\n\n");
    if (maxStepsParser.indexIn(m_optimizeGeometryLog) != -1) {
      m_optimizeGeometryMaxSteps = maxStepsParser.cap(1).toInt();
      emit optimizeGeometryStatusUpdate(0, m_optimizeGeometryMaxSteps,
                                        0.0, 0.0);
    }
  }

  // Emit the last printed step
  if (m_optimizeGeometryMaxSteps >= 0) {
    QRegExp lastStepParser("\\n\\s*([0-9]+)\\s+([-0-9.]+)\\s+([-0-9.]+)\\n");
    if (lastStepParser.lastIndexIn(m_optimizeGeometryLog) != -1) {
     int step = lastStepParser.cap(1).toInt();
     double energy = lastStepParser.cap(2).toDouble();
     double lastEnergy = lastStepParser.cap(3).toDouble();
     emit optimizeGeometryStatusUpdate(step, m_optimizeGeometryMaxSteps,
                                       energy, lastEnergy);
    }
  }
}

void OBProcess::executeObabel(const QStringList &options,
                              QObject *receiver, const char *slot,
                              const QByteArray &obabelStdin)
{
  // Setup exit handler
  connect(m_process, SIGNAL(finished(int)), receiver, slot);
  connect(m_process, SIGNAL(error(QProcess::ProcessError)), receiver, slot);

  // Start process
  qDebug() << "OBProcess::executeObabel: "
              "Running" << m_obabelExecutable << options.join(" ");
  m_process->start(m_obabelExecutable, options);
  if (!obabelStdin.isNull()) {
    m_process->write(obabelStdin);
    m_process->closeWriteChannel();
  }
}

void OBProcess::resetState()
{
  m_aborted = false;
  m_process->disconnect(this);
  disconnect(m_process);
  connect(this, SIGNAL(aborted()), m_process, SLOT(kill()));
}

} // namespace QtPlugins
} // namespace Avogadro