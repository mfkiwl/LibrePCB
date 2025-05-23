/*
 * LibrePCB - Professional EDA for everyone!
 * Copyright (C) 2013 LibrePCB Developers, see AUTHORS.md for contributors.
 * https://librepcb.org/
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*******************************************************************************
 *  Includes
 ******************************************************************************/
#include "transactionalfilesystem.h"

#include "../serialization/sexpression.h"
#include "../utils/toolbox.h"
#include "fileutils.h"
#include "ziparchive.h"
#include "zipwriter.h"

/*******************************************************************************
 *  Namespace
 ******************************************************************************/
namespace librepcb {

/*******************************************************************************
 *  Constructors / Destructor
 ******************************************************************************/

TransactionalFileSystem::TransactionalFileSystem(
    const FilePath& filepath, bool writable, RestoreCallback restoreCallback,
    DirectoryLock::LockHandlerCallback lockCallback, QObject* parent)
  : FileSystem(parent),
    mFilePath(filepath),
    mIsWritable(writable),
    mLock(filepath),
    mRestoredFromAutosave(false),
    mMutex() {
  // Load the backup if there is one (i.e. last save operation has failed).
  FilePath backupFile = mFilePath.getPathTo(".backup/backup.lp");
  if (backupFile.isExistingFile()) {
    qDebug() << "Restore file system from backup:" << backupFile.toNative();
    loadDiff(backupFile);  // can throw
  }

  // Lock directory if the file system is opened in R/W mode.
  if (mIsWritable) {
    FileUtils::makePath(mFilePath);  // can throw
    mLock.tryLock(lockCallback);  // can throw
  }

  // If there is an autosave backup, load it according the restore mode.
  FilePath autosaveFile = mFilePath.getPathTo(".autosave/autosave.lp");
  if (autosaveFile.isExistingFile()) {
    if (restoreCallback && restoreCallback(mFilePath)) {  // can throw
      qDebug() << "Restore file system from autosave backup:"
               << autosaveFile.toNative();
      loadDiff(autosaveFile);  // can throw
      mRestoredFromAutosave = true;
    }
  }
}

TransactionalFileSystem::~TransactionalFileSystem() noexcept {
  // Remove autosave directory as it is not needed in case the file system
  // was gracefully closed. We only need it if the application has crashed.
  // But if the file system is opened in read-only mode, or if an autosave was
  // restored but not saved in the meantime, do NOT remove the autosave
  // directory!
  if (mIsWritable && (!mRestoredFromAutosave)) {
    try {
      removeDiff("autosave");  // can throw
    } catch (const Exception& e) {
      qWarning() << "Failed to remove autosave directory:" << e.getMsg();
    }
  }
}

/*******************************************************************************
 *  Inherited from FileSystem
 ******************************************************************************/

FilePath TransactionalFileSystem::getAbsPath(
    const QString& path) const noexcept {
  return mFilePath.getPathTo(cleanPath(path));
}

QStringList TransactionalFileSystem::getDirs(
    const QString& path) const noexcept {
  QSet<QString> dirnames;
  QString dirpath = cleanPath(path);
  if (!dirpath.isEmpty()) dirpath.append("/");
  QMutexLocker lock(&mMutex);

  // add directories from file system, if not removed
  QDir dir(mFilePath.getPathTo(path).toStr());
  foreach (const QString& dirname,
           dir.entryList(QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot)) {
    if (!isRemoved(dirpath % dirname % "/")) {
      dirnames.insert(dirname);
    }
  }

  // add directories of new files
  foreach (const QString& filepath, mModifiedFiles.keys()) {
    if (filepath.startsWith(dirpath)) {
      QStringList relpath = filepath.mid(dirpath.length()).split('/');
      if (relpath.count() > 1) {
        dirnames.insert(relpath.first());
      }
    }
  }

  return dirnames.values();
}

QStringList TransactionalFileSystem::getFiles(
    const QString& path) const noexcept {
  QSet<QString> filenames;
  QString dirpath = cleanPath(path);
  if (!dirpath.isEmpty()) dirpath.append("/");
  QMutexLocker lock(&mMutex);

  // add files from file system, if not removed
  QDir dir(mFilePath.getPathTo(path).toStr());
  foreach (const QString& filename, dir.entryList(QDir::Files | QDir::Hidden)) {
    if (!isRemoved(dirpath % filename)) {
      filenames.insert(filename);
    }
  }

  // add new files
  foreach (const QString& filepath, mModifiedFiles.keys()) {
    if (filepath.startsWith(dirpath)) {
      QStringList relpath = filepath.mid(dirpath.length()).split('/');
      if (relpath.count() == 1) {
        filenames.insert(relpath.first());
      }
    }
  }

  return filenames.values();
}

bool TransactionalFileSystem::fileExists(const QString& path) const noexcept {
  const QString cleanedPath = cleanPath(path);
  QMutexLocker lock(&mMutex);
  if (mModifiedFiles.contains(cleanedPath)) {
    return true;
  } else if (isRemoved(cleanedPath)) {
    return false;
  } else {
    return mFilePath.getPathTo(cleanedPath).isExistingFile();
  }
}

QByteArray TransactionalFileSystem::read(const QString& path) const {
  const QByteArray content = readIfExists(path);
  if (content.isNull()) {
    throw RuntimeError(
        __FILE__, __LINE__,
        tr("File '%1' does not exist.")
            .arg(mFilePath.getPathTo(cleanPath(path)).toNative()));
  }
  return content;
}

QByteArray TransactionalFileSystem::readIfExists(const QString& path) const {
  const QString cleanedPath = cleanPath(path);
  QMutexLocker lock(&mMutex);
  if (mModifiedFiles.contains(cleanedPath)) {
    return mModifiedFiles.value(cleanedPath);
  } else if (!isRemoved(cleanedPath)) {
    const FilePath fp = mFilePath.getPathTo(cleanedPath);
    if (fp.isExistingFile()) {
      return FileUtils::readFile(fp);  // can throw
    }
  }
  return QByteArray();
}

void TransactionalFileSystem::write(const QString& path,
                                    const QByteArray& content) {
  const QString cleanedPath = cleanPath(path);
  QMutexLocker lock(&mMutex);
  mModifiedFiles[cleanedPath] = content;
  mRemovedFiles.remove(cleanedPath);
}

void TransactionalFileSystem::renameFile(const QString& src,
                                         const QString& dst) {
  write(dst, read(src));
  removeFile(src);
}

void TransactionalFileSystem::removeFile(const QString& path) {
  const QString cleanedPath = cleanPath(path);
  QMutexLocker lock(&mMutex);
  mModifiedFiles.remove(cleanedPath);
  mRemovedFiles.insert(cleanedPath);
}

void TransactionalFileSystem::removeDirRecursively(const QString& path) {
  QString dirpath = cleanPath(path);
  if (!dirpath.isEmpty()) dirpath.append("/");
  QMutexLocker lock(&mMutex);
  foreach (const QString& fp, mModifiedFiles.keys()) {
    if (dirpath.isEmpty() || fp.startsWith(dirpath)) {
      mModifiedFiles.remove(fp);
    }
  }
  foreach (const QString& fp, mRemovedFiles) {
    if (dirpath.isEmpty() || fp.startsWith(dirpath)) {
      mRemovedFiles.remove(fp);
    }
  }
  mRemovedDirs.insert(dirpath);
}

/*******************************************************************************
 *  General Methods
 ******************************************************************************/

void TransactionalFileSystem::loadFromZip(QByteArray content) {
  ZipArchive zip(content);  // can throw

  QMutexLocker lock(&mMutex);
  for (std::size_t i = 0; i < zip.getEntriesCount(); ++i) {
    const QString fileName = zip.getFileName(i);  // can throw
    if ((!fileName.endsWith("/")) && (!fileName.endsWith("\\"))) {
      write(fileName, zip.readFile(i));  // can throw
    }
  }
}

void TransactionalFileSystem::loadFromZip(const FilePath& fp) {
  ZipArchive zip(fp);  // can throw

  QMutexLocker lock(&mMutex);
  for (std::size_t i = 0; i < zip.getEntriesCount(); ++i) {
    const QString fileName = zip.getFileName(i);  // can throw
    if ((!fileName.endsWith("/")) && (!fileName.endsWith("\\"))) {
      write(fileName, zip.readFile(i));  // can throw
    }
  }
}

QByteArray TransactionalFileSystem::exportToZip(FilterFunction filter) const {
  ZipWriter zip;  // can throw
  QMutexLocker lock(&mMutex);
  exportDirToZip(zip, FilePath(), "", filter);  // can throw
  zip.finish();  // can throw
  return zip.getData();  // can throw
}

void TransactionalFileSystem::exportToZip(const FilePath& fp,
                                          FilterFunction filter) const {
  try {
    ZipWriter zip(fp);  // can throw
    QMutexLocker lock(&mMutex);
    exportDirToZip(zip, fp, "", filter);  // can throw
    zip.finish();  // can throw
  } catch (const Exception& e) {
    // Remove ZIP file because it is not complete
    QFile(fp.toStr()).remove();
    throw;
  }
}

void TransactionalFileSystem::discardChanges() noexcept {
  QMutexLocker lock(&mMutex);
  mModifiedFiles.clear();
  mRemovedFiles.clear();
  mRemovedDirs.clear();
}

QStringList TransactionalFileSystem::checkForModifications() const {
  QMutexLocker lock(&mMutex);
  QStringList modifications;

  // removed directories
  foreach (const QString& dir, mRemovedDirs) {
    FilePath fp = mFilePath.getPathTo(dir);
    if (fp.isExistingDir()) {
      modifications.append(dir);
    }
  }

  // removed files
  foreach (const QString& filepath, mRemovedFiles) {
    FilePath fp = mFilePath.getPathTo(filepath);
    if (fp.isExistingFile()) {
      modifications.append(filepath);
    }
  }

  // new or modified files
  foreach (const QString& filepath, mModifiedFiles.keys()) {
    FilePath fp = mFilePath.getPathTo(filepath);
    QByteArray content = mModifiedFiles.value(filepath);
    if ((!fp.isExistingFile()) ||
        (FileUtils::readFile(fp) != content)) {  // can throw
      modifications.append(filepath);
    }
  }

  return modifications;
}

void TransactionalFileSystem::autosave() {
  QMutexLocker lock(&mMutex);
  saveDiff("autosave");  // can throw
}

void TransactionalFileSystem::save() {
  QMutexLocker lock(&mMutex);

  // save to backup directory
  saveDiff("backup");  // can throw

  // modifications are now saved to the backup directory, so there is no risk
  // of losing a restored autosave backup, thus we can reset its flag
  mRestoredFromAutosave = false;

  // remove autosave directory because it is now older than the backup content
  // (the user should not be able to restore the outdated autosave backup)
  removeDiff("autosave");  // can throw

  // remove directories
  foreach (const QString& dir, mRemovedDirs) {
    FilePath fp = mFilePath.getPathTo(dir);
    if (fp.isExistingDir()) {
      FileUtils::removeDirRecursively(fp);  // can throw
    }
  }

  // remove files
  foreach (const QString& filepath, mRemovedFiles) {
    FilePath fp = mFilePath.getPathTo(filepath);
    if (fp.isExistingFile()) {
      FileUtils::removeFile(fp);  // can throw
    }
  }

  // save new or modified files
  foreach (const QString& filepath, mModifiedFiles.keys()) {
    FileUtils::writeFile(mFilePath.getPathTo(filepath),
                         mModifiedFiles.value(filepath));  // can throw
  }

  // remove backup
  removeDiff("backup");  // can throw

  // clear state
  discardChanges();
}

void TransactionalFileSystem::releaseLock() {
  mIsWritable = false;
  mLock.unlockIfLocked();  // can throw
}

/*******************************************************************************
 *  Static Methods
 ******************************************************************************/

QString TransactionalFileSystem::cleanPath(QString path) noexcept {
  return path.trimmed()
      .replace('\\', '/')
      .split('/', Qt::SkipEmptyParts)
      .join('/')
      .trimmed();
}

/*******************************************************************************
 *  Private Methods
 ******************************************************************************/

bool TransactionalFileSystem::isRemoved(const QString& path) const noexcept {
  if (mRemovedFiles.contains(path)) {
    return true;
  }

  foreach (const QString dir, mRemovedDirs) {
    if (path.startsWith(dir)) {
      return true;
    }
  }

  return false;
}

void TransactionalFileSystem::exportDirToZip(ZipWriter& zip,
                                             const FilePath& zipFp,
                                             const QString& dir,
                                             FilterFunction filter) const {
  QString path = dir.isEmpty() ? dir : dir % "/";

  // export directories
  foreach (const QString& dirname, getDirs(dir)) {
    // skip dotdirs, e.g. ".git", ".svn", ".autosave", ".backup"
    if (dirname.startsWith('.')) continue;
    exportDirToZip(zip, zipFp, path % dirname, filter);
  }

  // export files
  foreach (const QString& filename, getFiles(dir)) {
    QString filepath = path % filename;
    if (zipFp.isValid() && (filepath == zipFp.toRelative(mFilePath))) {
      // In case the exported ZIP file is located inside this file system,
      // we have to skip it. Otherwise we would get a ZIP inside the ZIP file.
      continue;
    }
    // skip lock file
    if (filename == ".lock") continue;
    // apply custom filter
    if (filter && (!filter(filepath))) continue;
    // read file content and add it to the ZIP archive
    const QByteArray& content = read(filepath);  // can throw
    // write to zip
    zip.writeFile(filepath, content, 0644);  // can throw
  }
}

void TransactionalFileSystem::saveDiff(const QString& type) const {
  QDateTime dt = QDateTime::currentDateTime();
  FilePath dir = mFilePath.getPathTo("." % type);
  FilePath filesDir = dir.getPathTo(dt.toString("yyyy-MM-dd_hh-mm-ss-zzz"));

  if (!mIsWritable) {
    throw RuntimeError(__FILE__, __LINE__, tr("File system is read-only."));
  }

  std::unique_ptr<SExpression> root =
      SExpression::createList("librepcb_" % type);
  root->ensureLineBreak();
  root->appendChild("created", dt);
  root->ensureLineBreak();
  root->appendChild("modified_files_directory", filesDir.getFilename());
  foreach (const QString& filepath, Toolbox::sorted(mModifiedFiles.keys())) {
    root->ensureLineBreak();
    root->appendChild("modified_file", filepath);
    FileUtils::writeFile(filesDir.getPathTo(filepath),
                         mModifiedFiles.value(filepath));  // can throw
  }
  foreach (const QString& filepath, Toolbox::sorted(mRemovedFiles.values())) {
    root->ensureLineBreak();
    root->appendChild("removed_file", filepath);
  }
  foreach (const QString& filepath, Toolbox::sorted(mRemovedDirs.values())) {
    root->ensureLineBreak();
    root->appendChild("removed_directory", filepath);
  }
  root->ensureLineBreak();

  // Writing the main file must be the last operation to "mark" this diff as
  // complete!
  FileUtils::writeFile(dir.getPathTo(type % ".lp"),
                       root->toByteArray());  // can throw
}

void TransactionalFileSystem::loadDiff(const FilePath& fp) {
  discardChanges();  // get a clean state first

  const std::unique_ptr<const SExpression> root =
      SExpression::parse(FileUtils::readFile(fp), fp);  // can throw
  QString modifiedFilesDirName =
      root->getChild("modified_files_directory/@0").getValue();
  FilePath modifiedFilesDir = fp.getParentDir().getPathTo(modifiedFilesDirName);
  foreach (const SExpression* node, root->getChildren("modified_file")) {
    QString relPath = node->getChild("@0").getValue();
    FilePath absPath = modifiedFilesDir.getPathTo(relPath);
    mModifiedFiles.insert(relPath, FileUtils::readFile(absPath));  // can throw
  }
  foreach (const SExpression* node, root->getChildren("removed_file")) {
    QString relPath = node->getChild("@0").getValue();
    mRemovedFiles.insert(relPath);
  }
  foreach (const SExpression* node, root->getChildren("removed_directory")) {
    QString relPath = node->getChild("@0").getValue();
    mRemovedDirs.insert(relPath);
  }
}

void TransactionalFileSystem::removeDiff(const QString& type) {
  FilePath dir = mFilePath.getPathTo("." % type);
  FilePath file = dir.getPathTo(type % ".lp");

  // remove the index file first to mark the diff directory as incomplete
  if (file.isExistingFile()) {
    FileUtils::removeFile(file);  // can throw
  }

  // then remove the whole directory
  FileUtils::removeDirRecursively(dir);  // can throw
}

/*******************************************************************************
 *  End of File
 ******************************************************************************/

}  // namespace librepcb
