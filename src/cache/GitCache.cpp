#include "GitCache.h"

#include <QLogger.h>
#include <WipRevisionInfo.h>

using namespace QLogger;

GitCache::GitCache(QObject *parent)
   : QObject(parent)
   , mMutex(QMutex::Recursive)
{
}

GitCache::~GitCache()
{
   mCommits.clear();
   mCommitsMap.clear();
   mReferences.clear();
}

void GitCache::setup(const WipRevisionInfo &wipInfo, const QList<CommitInfo> &commits)
{
   QMutexLocker lock(&mMutex);

   const auto totalCommits = commits.count() + 1;

   QLog_Debug("Cache", QString("Configuring the cache for {%1} elements.").arg(totalCommits));

   mConfigured = false;

   mDirNames.clear();
   mFileNames.clear();
   mRevisionFilesMap.clear();
   mLanes.clear();

   if (mCommitsMap.isEmpty())
      mCommitsMap.reserve(totalCommits);

   if (mCommits.isEmpty() || totalCommits > mCommits.count())
      mCommits.resize(totalCommits);
   else if (totalCommits < mCommits.count())
   {
      const auto commitsToRemove = std::abs(totalCommits - mCommits.count());

      for (auto i = 0; i < commitsToRemove; ++i)
         mCommits.takeLast();
   }

   QLog_Debug("Cache", QString("Adding WIP revision."));

   insertWipRevision(wipInfo);

   auto count = 1;

   QLog_Debug("Cache", QString("Adding committed revisions."));

   for (const auto &commit : commits)
   {
      if (commit.isValid())
      {
         insertCommitInfo(commit, count);
         ++count;
      }
   }
}

CommitInfo GitCache::getCommitInfoByRow(int row)
{
   QMutexLocker lock(&mMutex);

   const auto commit = row >= 0 && row < mCommits.count() ? mCommits.at(row) : nullptr;

   return commit ? *commit : CommitInfo();
}

int GitCache::getCommitPos(const QString &sha)
{
   QMutexLocker lock(&mMutex);

   const auto iter = std::find_if(mCommitsMap.begin(), mCommitsMap.end(),
                                  [sha](const CommitInfo &commit) { return commit.sha().startsWith(sha); });

   if (iter != mCommitsMap.end())
      return mCommits.indexOf(&iter.value());

   return -1;
}

auto GitCache::searchCommit(const QString &text, const int startingPoint) const
{
   return std::find_if(mCommits.constBegin() + startingPoint, mCommits.constEnd(),
                       [text](CommitInfo *info) { return info->contains(text); });
}

auto GitCache::reverseSearchCommit(const QString &text, int startingPoint) const
{
   const auto startEndPos = startingPoint > 0 ? mCommits.count() - startingPoint + 1 : 0;

   return std::find_if(mCommits.crbegin() + startEndPos, mCommits.crend(),
                       [text](CommitInfo *info) { return info->contains(text); });
}

CommitInfo GitCache::searchCommitInfo(const QString &text, int startingPoint, bool reverse)
{
   QMutexLocker lock(&mMutex);
   CommitInfo commit;

   if (!reverse)
   {
      auto commitIter = searchCommit(text, startingPoint);

      if (commitIter == mCommits.constEnd())
         commitIter = searchCommit(text);

      if (commitIter != mCommits.constEnd())
         commit = **commitIter;
   }
   else
   {
      auto commitIter = reverseSearchCommit(text, startingPoint);

      if (commitIter == mCommits.crend())
         commitIter = reverseSearchCommit(text);

      if (commitIter != mCommits.crend())
         commit = **commitIter;
   }

   return commit;
}

CommitInfo GitCache::getCommitInfo(const QString &sha)
{
   QMutexLocker lock(&mMutex);

   if (!sha.isEmpty())
   {
      const auto c = mCommitsMap.value(sha, CommitInfo());

      if (!c.isValid())
      {
         const auto shas = mCommitsMap.keys();
         const auto it = std::find_if(shas.cbegin(), shas.cend(),
                                      [sha](const QString &shaToCompare) { return shaToCompare.startsWith(sha); });

         if (it != shas.cend())
            return mCommitsMap.value(*it);

         return CommitInfo();
      }

      return c;
   }

   return CommitInfo();
}

RevisionFiles GitCache::getRevisionFile(const QString &sha1, const QString &sha2) const
{
   return mRevisionFilesMap.value(qMakePair(sha1, sha2));
}

void GitCache::clearReferences()
{
   mReferences.clear();
}

void GitCache::insertCommitInfo(CommitInfo rev, int orderIdx)
{
   if (!mConfigured)
   {
      rev.setLanes(calculateLanes(rev));

      const auto sha = rev.sha();

      if (sha == mCommitsMap.value(CommitInfo::ZERO_SHA).parent(0))
         rev.addChildReference(&mCommitsMap[CommitInfo::ZERO_SHA]);

      mCommitsMap[sha] = rev;

      mCommits.replace(orderIdx, &mCommitsMap[sha]);

      if (mTmpChildsStorage.contains(sha))
      {
         for (auto &child : mTmpChildsStorage.values(sha))
            mCommitsMap[sha].addChildReference(child);

         mTmpChildsStorage.remove(sha);
      }

      const auto parents = mCommitsMap.value(sha).parents();

      for (const auto &parent : parents)
         mTmpChildsStorage.insert(parent, &mCommitsMap[sha]);
   }
}

void GitCache::insertWipRevision(const WipRevisionInfo &wipInfo)
{
   auto newParentSha = wipInfo.parentSha;

   QLog_Debug("Cache", QString("Updating the WIP commit. The actual parent has SHA {%1}.").arg(newParentSha));

   const auto key = qMakePair(CommitInfo::ZERO_SHA, newParentSha);
   const auto fakeRevFile = fakeWorkDirRevFile(wipInfo.diffIndex, wipInfo.diffIndexCached);

   insertRevisionFile(CommitInfo::ZERO_SHA, newParentSha, fakeRevFile);

   const auto log = fakeRevFile.count() == mUntrackedfiles.count() ? tr("No local changes") : tr("Local changes");

   QStringList parents;

   if (!newParentSha.isEmpty())
      parents.append(newParentSha);

   CommitInfo c(CommitInfo::ZERO_SHA, parents, QChar(), QStringLiteral("-"), QDateTime::currentDateTime(),
                QStringLiteral("-"), log);

   if (mLanes.isEmpty())
      mLanes.init(c.sha());

   c.setLanes(calculateLanes(c));

   if (mCommits[0])
      c.setLanes(mCommits[0]->getLanes());

   const auto sha = c.sha();

   mCommitsMap.insert(sha, std::move(c));
   mCommits[0] = &mCommitsMap[sha];
}

bool GitCache::insertRevisionFile(const QString &sha1, const QString &sha2, const RevisionFiles &file)
{
   const auto key = qMakePair(sha1, sha2);
   const auto emptyShas = !sha1.isEmpty() && !sha2.isEmpty();
   const auto isWip = sha1 == CommitInfo::ZERO_SHA;

   if ((emptyShas || isWip) && mRevisionFilesMap.value(key) != file)
   {
      QLog_Debug("Cache", QString("Adding the revisions files between {%1} and {%2}.").arg(sha1, sha2));

      mRevisionFilesMap.insert(key, file);

      return true;
   }

   return false;
}

void GitCache::insertReference(const QString &sha, References::Type type, const QString &reference)
{
   QMutexLocker lock(&mMutex);
   QLog_Debug("Cache", QString("Adding a new reference with SHA {%1}.").arg(sha));

   mReferences[sha].addReference(type, reference);
}

bool GitCache::hasReferences(const QString &sha) const
{
   return mReferences.contains(sha) && !mReferences.value(sha).isEmpty();
}

QStringList GitCache::getReferences(const QString &sha, References::Type type) const
{
   return mReferences.value(sha).getReferences(type);
}

void GitCache::reloadCurrentBranchInfo(const QString &currentBranch, const QString &currentSha)
{
   const auto lastItem = mReferences.end();
   for (auto ref = mReferences.begin(); ref != lastItem; ++ref)
   {
      if (ref.value().getReferences(References::Type::LocalBranch).contains(currentBranch))
      {
         ref.value().removeReference(References::Type::LocalBranch, currentBranch);

         const auto key = ref.key();

         if (mReferences.value(key).isEmpty())
            mReferences.remove(key);

         break;
      }
   }

   mReferences[currentSha].addReference(References::Type::LocalBranch, currentBranch);
}

bool GitCache::updateWipCommit(const WipRevisionInfo &wipInfo)
{
   if (mConfigured)
   {
      insertWipRevision(wipInfo);
      return true;
   }

   return false;
}

bool GitCache::containsRevisionFile(const QString &sha1, const QString &sha2) const
{
   return mRevisionFilesMap.contains(qMakePair(sha1, sha2));
}

QVector<Lane> GitCache::calculateLanes(const CommitInfo &c)
{
   const auto sha = c.sha();

   QLog_Trace("Cache", QString("Updating the lanes for SHA {%1}.").arg(sha));

   bool isDiscontinuity;
   bool isFork = mLanes.isFork(sha, isDiscontinuity);
   bool isMerge = c.parentsCount() > 1;

   if (isDiscontinuity)
      mLanes.changeActiveLane(sha); // uses previous isBoundary state

   if (isFork)
      mLanes.setFork(sha);
   if (isMerge)
      mLanes.setMerge(c.parents());
   if (c.parentsCount() == 0)
      mLanes.setInitial();

   const auto lanes = mLanes.getLanes();

   resetLanes(c, isFork);

   return lanes;
}

RevisionFiles GitCache::parseDiffFormat(const QString &buf, FileNamesLoader &fl, bool cached)
{
   RevisionFiles rf;
   auto parNum = 1;

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
   const auto lines = buf.split("\n", Qt::SkipEmptyParts);
#else
   const auto lines = buf.split("\n", QString::SkipEmptyParts);
#endif

   for (auto line : lines)
   {
      if (line[0] == ':') // avoid sha's in merges output
      {
         if (line[1] == ':')
         { // it's a combined merge
            /* For combined merges rename/copy information is useless
             * because nor the original file name, nor similarity info
             * is given, just the status tracks that in the left/right
             * branch a renamed/copy occurred (as example status could
             * be RM or MR). For visualization purposes we could consider
             * the file as modified
             */
            if (fl.rf != &rf && !cached)
            {
               flushFileNames(fl);
               fl.rf = &rf;
            }
            appendFileName(line.section('\t', -1), fl);
            rf.setStatus("M");
            rf.mergeParent.append(parNum);
         }
         else
         {
            if (line.at(98) == '\t') // Faster parsing in normal case
            {
               auto fields = line.split(" ");
               const auto dstSha = fields.at(3);
               auto fileIsCached = !dstSha.startsWith(QStringLiteral("000000"));
               const auto flag = fields.at(4).at(0);

               if (flag == 'D')
                  fileIsCached = !fileIsCached;

               if (fl.rf != &rf && (!cached || flag == 'U'))
               {
                  flushFileNames(fl);
                  fl.rf = &rf;
               }
               appendFileName(line.mid(99), fl);

               rf.setStatus(flag, fileIsCached);
               rf.mergeParent.append(parNum);
            }
            else // It's a rename or a copy, we are not in fast path now!
               setExtStatus(rf, line.mid(97), parNum, fl);
         }
      }
      else
         ++parNum;
   }

   return rf;
}

void GitCache::appendFileName(const QString &name, FileNamesLoader &fl)
{
   int idx = name.lastIndexOf('/') + 1;
   const QString &dr = name.left(idx);
   const QString &nm = name.mid(idx);

   auto it = mDirNames.indexOf(dr);
   if (it == -1)
   {
      int idx = mDirNames.count();
      mDirNames.append(dr);
      fl.rfDirs.append(idx);
   }
   else
      fl.rfDirs.append(it);

   it = mFileNames.indexOf(nm);
   if (it == -1)
   {
      int idx = mFileNames.count();
      mFileNames.append(nm);
      fl.rfNames.append(idx);
   }
   else
      fl.rfNames.append(it);

   fl.files.append(name);
}

void GitCache::flushFileNames(FileNamesLoader &fl)
{
   if (!fl.rf)
      return;

   for (auto i = 0; i < fl.rfNames.count(); ++i)
   {
      const auto dirName = mDirNames.at(fl.rfDirs.at(i));
      const auto fileName = mFileNames.at(fl.rfNames.at(i));

      if (!fl.rf->mFiles.contains(dirName + fileName))
         fl.rf->mFiles.append(dirName + fileName);
   }

   fl.rfNames.clear();
   fl.rfDirs.clear();
   fl.rf = nullptr;
}

bool GitCache::pendingLocalChanges()
{
   QMutexLocker lock(&mMutex);
   auto localChanges = false;

   const auto commit = mCommitsMap.value(CommitInfo::ZERO_SHA, CommitInfo());

   if (commit.isValid())
   {
      const auto rf = getRevisionFile(CommitInfo::ZERO_SHA, commit.parent(0));
      localChanges = rf.count() - mUntrackedfiles.count() > 0;
   }

   return localChanges;
}

QVector<QPair<QString, QStringList>> GitCache::getBranches(References::Type type)
{
   QMutexLocker lock(&mMutex);
   QVector<QPair<QString, QStringList>> branches;

   for (const auto &ref : mReferences.toStdMap())
      branches.append(QPair<QString, QStringList>(ref.first, ref.second.getReferences(type)));

   return branches;
}

QMap<QString, QString> GitCache::getTags(References::Type tagType) const
{
   QMap<QString, QString> tags;

   if (tagType == References::Type::LocalTag)
   {
      for (auto &reference : mReferences.toStdMap())
      {
         const auto tagNames = reference.second.getReferences(tagType);

         for (const auto &tag : tagNames)
            tags[tag] = reference.first;
      }
   }
   else
      tags = mRemoteTags;

   return tags;
}

void GitCache::updateTags(const QMap<QString, QString> &remoteTags)
{
   mRemoteTags = remoteTags;

   emit signalCacheUpdated();
}

void GitCache::addSubtrees(const QList<QPair<QString, QString>> &subtrees)
{
   for (auto &tree : subtrees)
      mSubtrees.append({ tree.first, tree.second });
}

QStringList GitCache::getSubtrees() const
{
   QStringList subtrees;

   for (auto &tree : mSubtrees)
      subtrees.append(tree.name);

   return subtrees;
}

void GitCache::setExtStatus(RevisionFiles &rf, const QString &rowSt, int parNum, FileNamesLoader &fl)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
   const QStringList sl(rowSt.split('\t', Qt::SkipEmptyParts));
#else
   const QStringList sl(rowSt.split('\t', QString::SkipEmptyParts));
#endif
   if (sl.count() != 3)
      return;

   // we want store extra info with format "orig --> dest (Rxx%)"
   // but git give us something like "Rxx\t<orig>\t<dest>"
   QString type = sl[0];
   type.remove(0, 1);
   const QString &orig = sl[1];
   const QString &dest = sl[2];
   const QString extStatusInfo(orig + " --> " + dest + " (" + QString::number(type.toInt()) + "%)");

   /*
    NOTE: we set rf.extStatus size equal to position of latest
          copied/renamed file. So it can have size lower then
          rf.count() if after copied/renamed file there are
          others. Here we have no possibility to know final
          dimension of this RefFile. We are still in parsing.
 */

   // simulate new file
   if (fl.rf != &rf)
   {
      flushFileNames(fl);
      fl.rf = &rf;
   }
   appendFileName(dest, fl);
   rf.mergeParent.append(parNum);
   rf.setStatus(RevisionFiles::NEW);
   rf.appendExtStatus(extStatusInfo);

   // simulate deleted orig file only in case of rename
   if (type.at(0) == 'R')
   { // renamed file
      if (fl.rf != &rf)
      {
         flushFileNames(fl);
         fl.rf = &rf;
      }
      appendFileName(orig, fl);
      rf.mergeParent.append(parNum);
      rf.setStatus(RevisionFiles::DELETED);
      rf.appendExtStatus(extStatusInfo);
   }
   rf.setOnlyModified(false);
}

void GitCache::resetLanes(const CommitInfo &c, bool isFork)
{
   const auto nextSha = c.parentsCount() == 0 ? QString() : c.parent(0);

   mLanes.nextParent(nextSha);

   if (c.parentsCount() > 1)
      mLanes.afterMerge();
   if (isFork)
      mLanes.afterFork();
   if (mLanes.isBranch())
      mLanes.afterBranch();
}

int GitCache::count() const
{
   return mCommits.count();
}

RevisionFiles GitCache::fakeWorkDirRevFile(const QString &diffIndex, const QString &diffIndexCache)
{
   FileNamesLoader fl;
   auto rf = parseDiffFormat(diffIndex, fl);
   fl.rf = &rf;
   rf.setOnlyModified(false);

   for (const auto &it : qAsConst(mUntrackedfiles))
   {
      if (fl.rf != &rf)
      {
         flushFileNames(fl);
         fl.rf = &rf;
      }

      appendFileName(it, fl);
      rf.setStatus(RevisionFiles::UNKNOWN);
      rf.mergeParent.append(1);
   }

   RevisionFiles cachedFiles = parseDiffFormat(diffIndexCache, fl, true);
   flushFileNames(fl);

   for (auto i = 0; i < rf.count(); i++)
   {
      if (cachedFiles.mFiles.indexOf(rf.getFile(i)) != -1)
      {
         if (cachedFiles.statusCmp(i, RevisionFiles::CONFLICT))
            rf.appendStatus(i, RevisionFiles::CONFLICT);
         else if (rf.statusCmp(i, RevisionFiles::MODIFIED) && !rf.statusCmp(i, RevisionFiles::IN_INDEX))
            rf.appendStatus(i, RevisionFiles::PARTIALLY_CACHED);
      }
   }

   return rf;
}

RevisionFiles GitCache::parseDiff(const QString &logDiff)
{
   FileNamesLoader fl;

   auto rf = parseDiffFormat(logDiff, fl);
   fl.rf = &rf;
   flushFileNames(fl);

   return rf;
}

void GitCache::setUntrackedFilesList(const QVector<QString> &untrackedFiles)
{
   mUntrackedfiles = untrackedFiles;
}
