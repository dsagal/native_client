#!/usr/bin/python
# Copyright (c) 2014 The Native Client Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This script handles all of the processing for versioning packages.

package_version.py manages all of the various operations done between
packages, including archiving, extracting, uploading, and downloading
packages. For a list of options and commands, see the help for the script.

Glossary:
  Package: A list of archives, such as "nacl_x86_glibc" or "nacl_x86_newlib".
  Package Archive: An archive (usually a tar file) that is part of a package.
  Package Target: Package targets consists of packages. Each package target
    has it's own version of a package. An example of a package target would
    be something such as "win_x86_nacl_x86" or "mac_x86_nacl_x86". In that case,
    "win_x86_nacl_x86" and "mac_x86_nacl_x86" would each have their own version
    of "nacl_x86_glibc" and "nacl_x86_newlib" for windows and mac respectively.
  Revision Number: The SVN revision number of a sanctioned version. This number
    is used to synchronize packages to sanctioned versions.

JSON Files:
  Packages File - A file which describes the various package targets for each
    platform/architecture along with the packages associated with each package
    target.
    [Default file: build/package_version/standard_packages.json].
  Package File - A file which contains the list of package archives within
    a package.
    [Default file: toolchain/.tars/$PACKAGE_TARGET/$PACKAGE.json]
  Archive File - A file which describes an archive within a package. Each
    archive description file will contain information about an archive such
    as name, URL to download from, and hash.
    [Default File: toolchain/.tars/$PACKAGE_TARGET/$PACKAGE/$ARCHIVE.json]
  Revision File - A file which describes the sanctioned version of package
    for each of the package targets associated with it.
    [Default file: toolchain_revisions/$PACKAGE.json]
"""

import argparse
import collections
import logging
import os
import shutil
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.append(os.path.dirname(SCRIPT_DIR))
import cygtar

sys.path.append(os.path.dirname(os.path.dirname(SCRIPT_DIR)))
import pynacl.file_tools
import pynacl.gsd_storage
import pynacl.log_tools
import pynacl.platform
import pynacl.working_directory

import archive_info
import error
import package_info
import package_locations
import packages_info
import revision_info

CURRENT_DIR = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR = os.path.dirname(CURRENT_DIR)
NACL_DIR = os.path.dirname(BUILD_DIR)

TEMP_SUFFIX = '.tmp'

DEFAULT_PACKAGES_JSON = os.path.join(CURRENT_DIR, 'standard_packages.json')
DEFAULT_REVISIONS_DIR = os.path.join(NACL_DIR, 'toolchain_revisions')
DEFAULT_DEST_DIR = os.path.join(NACL_DIR, 'toolchain')
DEFAULT_CLOUD_BUCKET = 'nativeclient-archive2'


#
# These are helper functions that help each command.
#

def CleanTempFiles(directory):
  """Cleans up all temporary files ending with TEMP_SUFFIX in a directory."""
  for root, dirs, files in os.walk(directory):
    for file_name in files:
      if file_name.endswith(TEMP_SUFFIX):
        file_path = os.path.join(root, file_name)
        os.unlink(file_path)


def GetPackageTargetPackages(custom_package_name, package_target_packages):
  """Returns a list of package target packages given a custom package name.

  A custom package name can either have a specified package target attached
  to it (IE. $PACKAGE_TARGET/PACKAGE_NAME) or be extracted out of a default
  list of package targets along with their packages.

  Args:
    custom_package_name: Package name with an optional package target.
    package_target_packages: List of tuples (package_target, package).
  Returns:
    List of package target tuples matching the package name.
  """
  package_path = custom_package_name.replace('\\', os.path.sep)
  package_path = package_path.replace('/', os.path.sep)
  if os.path.sep in package_path:
    # Package target is part of the custom package name, just return it.
    package_target, package_name = package_path.split(os.path.sep, 1)
    return [(package_target, package_name)]

  # Package target is not part of the package name, filter from list of passed
  # in package targets.
  return [
      (package_target, package)
      for package_target, package in package_target_packages
      if package == custom_package_name
  ]


def DownloadPackageArchives(tar_dir, package_target, package_name, package_desc,
                            downloader=None, revision_num=None,
                            include_logs=False):
  """Downloads package archives from the cloud to the tar directory.

  Args:
    tar_dir: Root tar directory where archives will be downloaded to.
    package_target: Package target of the package to download.
    package_name: Package name of the package to download.
    package_desc: package_info object of the package to download.
    downloader: function which takes a url and a file path for downloading.
  Returns:
    The list of files that were downloaded.
  """
  downloaded_files = []
  if downloader is None:
    downloader = pynacl.gsd_storage.HttpDownload
  local_package_file = package_locations.GetLocalPackageFile(tar_dir,
                                                             package_target,
                                                             package_name)
  # To ensure that we do not redownload extra archives that we already have,
  # create a dictionary of old package archives that contains the hash of each
  # package archive.
  old_archives = {}
  if os.path.isfile(local_package_file):
    try:
      old_package_desc = package_info.PackageInfo(local_package_file)
      old_archives_list = old_package_desc.GetArchiveList()
      old_archive_names = [archive.GetArchiveData().name
                           for archive
                           in old_archives_list]
      for archive_name in old_archive_names:
        archive_file = package_locations.GetLocalPackageArchiveFile(
            tar_dir,
            package_target,
            package_name,
            archive_name
            )

        archive_hash = archive_info.GetArchiveHash(archive_file)
        if archive_hash is not None:
          old_archives[archive_name] = archive_hash
    except:
      # Nothing can be trusted here anymore, delete all package archives.
      archive_directory = package_locations.GetLocalPackageArchiveDir(
          tar_dir,
          package_target,
          package_name
          )
      os.unlink(local_package_file)
      pynacl.file_tools.RemoveDir(archive_directory)

  # Download packages information file along with each of the package
  # archives described in the information file. Also keep track of what
  # new package names matches old package names. We will have to delete
  # stale package names after we are finished.
  update_archives = []
  for archive_obj in package_desc.GetArchiveList():
    archive_desc = archive_obj.GetArchiveData()
    old_hash = old_archives.get(archive_desc.name, None)
    if old_hash is not None:
      old_archives.pop(archive_desc.name)
      if archive_desc.hash == old_hash:
        logging.debug('Skipping matching archive: %s', archive_desc.name)
        continue
    update_archives.append(archive_obj)

  if update_archives:
    logging.info('--Syncing %s to revision %s--' % (package_name, revision_num))
    num_archives = len(update_archives)
    for index, archive_obj in enumerate(update_archives):
      archive_desc = archive_obj.GetArchiveData()
      local_archive_file = package_locations.GetLocalPackageArchiveFile(
          tar_dir,
          package_target,
          package_name,
          archive_desc.name
      )
      pynacl.file_tools.MakeParentDirectoryIfAbsent(local_archive_file)

      if archive_desc.url is None:
        raise error.Error('Error, no URL for archive: %s' % archive_desc.name)

      logging.info('Downloading package archive: %s (%d/%d)' %
                   (archive_desc.name, index+1, num_archives))
      try:
        downloader(archive_desc.url, local_archive_file)
      except Exception as e:
        raise error.Error('Could not download URL (%s): %s' %
                          (archive_desc.url, e))

      # Delete any stale log files
      local_archive_log = package_locations.GetLocalPackageArchiveLogFile(
          local_archive_file
      )
      if os.path.isfile(local_archive_log):
        os.unlink(local_archive_log)

      verified_hash = archive_info.GetArchiveHash(local_archive_file)
      if verified_hash != archive_desc.hash:
        raise error.Error('Package hash check failed: %s != %s' %
                          (verified_hash, archive_desc.hash))

      downloaded_files.append(local_archive_file)

  # Download any logs if include_logs is True.
  if include_logs:
    download_logs = []
    for archive_obj in package_desc.GetArchiveList():
      archive_desc = archive_obj.GetArchiveData()
      if archive_desc.log_url:
        local_archive_file = package_locations.GetLocalPackageArchiveFile(
            tar_dir,
            package_target,
            package_name,
            archive_desc.name
        )
        local_archive_log = package_locations.GetLocalPackageArchiveLogFile(
            local_archive_file
        )
        if not os.path.isfile(local_archive_log):
          download_log_tuple = (archive_desc.name,
                                archive_desc.log_url,
                                local_archive_log)
          download_logs.append(download_log_tuple)

    if download_logs:
      logging.info('--Syncing %s Logs--' % (package_name))
      num_logs = len(download_logs)
      for index, download_log_tuple in enumerate(download_logs):
        name, log_url, local_log_file = download_log_tuple
        logging.info('Downloading archive log: %s (%d/%d)' %
                     (name, index+1, num_logs))

        try:
          downloader(log_url, local_log_file)
        except Exception as e:
          raise IOError('Could not download log URL (%s): %s' %
                        (log_url, e))

  # Delete any stale left over packages.
  for old_archive in old_archives:
    archive_file = package_locations.GetLocalPackageArchiveFile(
        tar_dir,
        package_target,
        package_name,
        old_archive)
    os.unlink(archive_file)

    archive_log = package_locations.GetLocalPackageArchiveLogFile(archive_file)
    if os.path.isfile(archive_log):
      os.unlink(archive_log)

  # Save the package file so we know what we currently have.
  if update_archives or old_archives:
    package_desc.SavePackageFile(local_package_file)

  return downloaded_files


def ArchivePackageArchives(tar_dir, package_target, package_name, archives,
                           extra_archives=[]):
  """Archives local package archives to the tar directory.

  Args:
    tar_dir: Root tar directory where archives live.
    package_target: Package target of the package to archive.
    package_name: Package name of the package to archive.
    archives: List of archive file paths where archives currently live.
    extra_archives: Extra archives that are expected to be build elsewhere.
  Returns:
    Returns the local package file that was archived.
  """
  local_package_file = package_locations.GetLocalPackageFile(tar_dir,
                                                             package_target,
                                                             package_name)

  valid_archive_files = set()
  archive_list = []

  package_desc = package_info.PackageInfo()
  package_archives = ([(archive, False) for archive in archives] +
                      [(archive, True) for archive in extra_archives])
  for archive, skip_missing in package_archives:
    archive_url = None
    archive_log_url = None
    if '@' in archive:
      archive, archive_url = archive.split('@', 1)
      if ',' in archive_url:
        archive_url, archive_log_url = archive_url.split(',', 1)

    extract_param = ''
    tar_src_dir = ''
    extract_dir = ''
    if ',' in archive:
      archive, extract_param = archive.split(',', 1)
      if ':' in extract_param:
        tar_src_dir, extract_dir = extract_param.split(':', 1)
      else:
        tar_src_dir = extract_param

    archive_hash = archive_info.GetArchiveHash(archive)
    archive_name = os.path.basename(archive)
    archive_desc = archive_info.ArchiveInfo(name=archive_name,
                                            hash=archive_hash,
                                            url=archive_url,
                                            tar_src_dir=tar_src_dir,
                                            extract_dir=extract_dir,
                                            log_url=archive_log_url)
    package_desc.AppendArchive(archive_desc)

    if archive_hash is None:
      if skip_missing:
        logging.info('Skipping archival of missing file: %s', archive)
        continue
      raise error.Error('Invalid package: %s.' % archive)
    archive_list.append(archive)

    archive_basename = os.path.basename(archive)
    archive_json = archive_basename + '.json'
    valid_archive_files.update([archive_basename, archive_json])

  # Delete any stale archive files
  local_archive_dir = package_locations.GetLocalPackageArchiveDir(
      tar_dir,
      package_target,
      package_name)

  if os.path.isdir(local_archive_dir):
    for dir_item in os.listdir(local_archive_dir):
      if dir_item in valid_archive_files:
        continue

      item_path = os.path.join(local_archive_dir, dir_item)
      if os.path.isdir(item_path):
        pynacl.file_tools.RemoveDir(item_path)
      else:
        pynacl.file_tools.RemoveFile(item_path)

  # We do not need to archive the package if it already matches. But if the
  # local package file is invalid or does not match, then we should recreate
  # the json file.
  if os.path.isfile(local_package_file):
    try:
      current_package_desc = package_info.PackageInfo(local_package_file,
                                                      skip_missing=True)
      if current_package_desc == package_desc:
        return
    except ValueError:
      pass

  # Copy each of the packages over to the tar directory first.
  for archive_file in archive_list:
    archive_name = os.path.basename(archive_file)
    local_archive_file = package_locations.GetLocalPackageArchiveFile(
        tar_dir,
        package_target,
        package_name,
        archive_name)

    logging.info('Archiving file: %s', archive_file)
    pynacl.file_tools.MakeParentDirectoryIfAbsent(local_archive_file)
    shutil.copyfile(archive_file, local_archive_file)

  # Once all the copying is completed, update the local packages file.
  logging.info('Package "%s" archived: %s', package_name, local_package_file)
  pynacl.file_tools.MakeParentDirectoryIfAbsent(local_package_file)
  package_desc.SavePackageFile(local_package_file)

  return local_package_file


def UploadPackage(storage, revision, tar_dir, package_target, package_name,
                  is_shared_package, annotate=False, skip_missing=False,
                  custom_package_file=None):
  """Uploads a local package file to the supplied cloud storage object.

  By default local package files are expected to be found in the standardized
  location within the tar directory, however a custom package file may be
  specified to upload from a different location. Package archives that do not
  have their URL field set will automaticaly have the archives uploaded so that
  someone accessing the package file from the cloud storage will also have
  access to the package archives.

  Args:
    storage: Cloud storage object which supports PutFile and GetFile.
    revision: SVN Revision number the package should be associated with.
    tar_dir: Root tar directory where archives live.
    package_target: Package target of the package to archive.
    package_name: Package name of the package to archive.
    is_shared_package: Is this package shared among all package targets?
    annotate: Print annotations for build bots?
    skip_missing: Skip missing package archive files?
    custom_package_file: File location for a custom package file.
  Returns:
    Returns remote download key for the uploaded package file.
  """
  if custom_package_file is not None:
    local_package_file = custom_package_file
  else:
    local_package_file = package_locations.GetLocalPackageFile(
        tar_dir,
        package_target,
        package_name)

  # Upload the package file and also upload any local package archives so
  # that they are downloadable.
  package_desc = package_info.PackageInfo(local_package_file,
                                          skip_missing=skip_missing)
  upload_package_desc = package_info.PackageInfo()

  for archive_obj in package_desc.GetArchiveList():
    archive_desc = archive_obj.GetArchiveData()
    url = archive_desc.url
    if archive_desc.hash and url is None:
      if annotate:
        print '@@@BUILD_STEP Archive:%s (upload)@@@' % archive_desc.name

      archive_file = package_locations.GetLocalPackageArchiveFile(
          tar_dir,
          package_target,
          package_name,
          archive_desc.name)
      archive_hash = archive_info.GetArchiveHash(archive_file)
      if archive_hash is None:
        raise error.Error('Missing Archive File: %s' % archive_file)
      elif archive_hash != archive_desc.hash:
        raise error.Error(
            'Archive hash does not match package hash: %s' % archive_file
            + '\n  Archive Hash: %s' % archive_hash
            + '\n  Package Hash: %s' % archive_desc.hash)

      logging.warn('Missing archive URL: %s', archive_desc.name)
      logging.warn('Uploading archive to be publically available...')
      remote_archive_key = package_locations.GetRemotePackageArchiveKey(
          archive_desc.name,
          archive_desc.hash)
      url = storage.PutFile(archive_file, remote_archive_key, clobber=True)
      if annotate:
        print '@@@STEP_LINK@download@%s@@@' % url

    updated_archive_obj = archive_obj.Copy(url=url)
    upload_package_desc.AppendArchive(updated_archive_obj)

  upload_package_file = local_package_file + '.upload'
  pynacl.file_tools.MakeParentDirectoryIfAbsent(upload_package_file)
  upload_package_desc.SavePackageFile(upload_package_file)

  logging.info('Uploading package information: %s', package_name)
  remote_package_key = package_locations.GetRemotePackageKey(
      is_shared_package,
      revision,
      package_target,
      package_name)
  package_info.UploadPackageInfoFiles(storage, package_target, package_name,
                                      remote_package_key, upload_package_file,
                                      skip_missing=skip_missing,
                                      annotate=annotate)

  return remote_package_key


def ExtractPackageTargets(package_target_packages, tar_dir, dest_dir,
                          downloader=None, skip_missing=False, quiet=False):
  """Extracts package targets from the tar directory to the destination.

  Each package archive within a package will be verified before being
  extracted. If a package archive does not exist or does not match the hash
  stored within the package file, it will be re-downloaded before being
  extracted.

  Args:
    package_target_packages: List of tuples of package target and package names.
    tar_dir: Source tar directory where package archives live.
    dest_dir: Root destination directory where packages will be extracted to.
    downloader: function which takes a url and a file path for downloading.
  """
  if downloader is None:
    downloader = pynacl.gsd_storage.HttpDownload

  for package_target, package_name in package_target_packages:
    package_file = package_locations.GetLocalPackageFile(tar_dir,
                                                         package_target,
                                                         package_name)
    package_desc = package_info.PackageInfo(package_file,
                                            skip_missing=skip_missing)
    dest_package_dir = package_locations.GetFullDestDir(dest_dir,
                                                        package_target,
                                                        package_name)
    dest_package_file = package_locations.GetDestPackageFile(dest_dir,
                                                             package_target,
                                                             package_name)

    # Only do the extraction if the extract packages do not match.
    if os.path.isfile(dest_package_file):
      try:
        dest_package_desc = package_info.PackageInfo(dest_package_file)
        if dest_package_desc == package_desc:
          logging.debug('Skipping extraction for package (%s)', package_name)
          continue
      except:
        # Destination package file cannot be trusted, if invalid re-extract.
        pass

      # Delete the old package file before we extract.
      os.unlink(dest_package_file)

    if os.path.isdir(dest_package_dir):
      logging.debug('Deleting old package directory: %s', dest_package_dir)
      pynacl.file_tools.RemoveDir(dest_package_dir)

    logging.info('Extracting package (%s) to directory: %s',
                 package_name, dest_package_dir)
    archive_list = package_desc.GetArchiveList()
    num_archives = len(archive_list)
    for index, archive_obj in enumerate(archive_list):
      archive_desc = archive_obj.GetArchiveData()
      archive_file = package_locations.GetLocalPackageArchiveFile(
          tar_dir,
          package_target,
          package_name,
          archive_desc.name
      )

      # Upon extraction, some files may not be downloaded (or have stale files),
      # we need to check the hash of each file and attempt to download it if
      # they do not match.
      archive_hash = archive_info.GetArchiveHash(archive_file)
      if archive_hash != archive_desc.hash:
        if archive_desc.url is None:
          if skip_missing:
            logging.info('Skipping extraction of missing archive: %s' %
                         archive_file)
            continue
          raise error.Error('Invalid archive file and URL: %s' % archive_file)

        logging.warn('Expected archive missing, downloading: %s',
                     archive_desc.name)

        pynacl.file_tools.MakeParentDirectoryIfAbsent(archive_file)
        downloader(archive_desc.url, archive_file)
        archive_hash = archive_info.GetArchiveHash(archive_file)
        if archive_hash != archive_desc.hash:
          raise error.Error('Downloaded archive file does not match hash.'
                      ' [%s] Expected %s, received %s.' %
                      (archive_file, archive_desc.hash, archive_hash))

      destination_dir = os.path.join(dest_package_dir, archive_desc.extract_dir)
      logging.info('Extracting %s (%d/%d)' %
                   (archive_desc.name, index+1, num_archives))

      temp_dir = os.path.join(destination_dir, '.tmp')
      pynacl.file_tools.RemoveDir(temp_dir)
      os.makedirs(temp_dir)
      tar_output = not quiet
      tar = cygtar.CygTar(archive_file, 'r:*', verbose=tar_output)
      curdir = os.getcwd()
      os.chdir(temp_dir)
      try:
        tar.Extract()
        tar.Close()
      finally:
        os.chdir(curdir)

      temp_src_dir = os.path.join(temp_dir, archive_desc.tar_src_dir)
      pynacl.file_tools.MoveAndMergeDirTree(temp_src_dir, destination_dir)
      pynacl.file_tools.RemoveDir(temp_dir)

    pynacl.file_tools.MakeParentDirectoryIfAbsent(dest_package_file)
    package_desc.SavePackageFile(dest_package_file)


#
# Each Command has 2 functions that describes it:
#   1. A parser function which specifies the extra command options each command
#   will have.
#   2. An execution function which is called when a user actually executes
#   the command.
#
def _ListCmdArgParser(subparser):
  subparser.description = 'Lists package information.'


def _DoListCmd(arguments):
  package_targets = collections.defaultdict(list)
  for package_target, package in arguments.package_target_packages:
    package_targets[package_target].append(package)

  print 'Listing Package Targets and Packages:'
  for package_target, packages in package_targets.iteritems():
    print '\n%s:' % package_target
    for package in sorted(packages):
      print ' ', package


def _ArchiveCmdArgParser(subparser):
  subparser.description = 'Archive package archives to tar directory.'
  subparser.add_argument(
    '--archive-package', metavar='NAME', dest='archive__package',
    required=True,
    help='Package name archives will be packaged into.')
  subparser.add_argument(
    '--extra-archive', metavar='ARCHIVE', dest='archive__extra_archive',
    action='append', default=[],
    help='Extra archives that are expected to be built elsewhere.')
  subparser.add_argument(
    metavar='TAR(,SRCDIR(:EXTRACTDIR))(@URL,LOGURL)', dest='archive__archives',
    nargs='+',
    help='Package archive with an optional tar information and url.'
         ' SRCDIR is the root directory where files live inside of the tar.'
         ' EXTRACTDIR is the directory to extract files to relative to the'
         ' destination directory. The URL is where the package can be'
         ' downloaded from.')
  subparser.add_argument(
    '-x', '--extract', dest='archive__extract',
    action='store_true', default=False,
    help='Extract package archives after they have been archived.')


def _DoArchiveCmd(arguments):
  package_target_packages = GetPackageTargetPackages(
      arguments.archive__package,
      arguments.package_target_packages
  )
  if not package_target_packages:
    raise error.Error('Unknown package: %s.' % arguments.archive__package
                + ' Did you forget to add "$PACKAGE_TARGET/"?')

  for package_target, package_name in package_target_packages:
    ArchivePackageArchives(arguments.tar_dir,
                           package_target,
                           package_name,
                           arguments.archive__archives,
                           extra_archives=arguments.archive__extra_archive)

    if arguments.archive__extract:
      ExtractPackageTargets([(package_target, package_name)],
                            arguments.tar_dir,
                            arguments.dest_dir,
                            skip_missing=True,
                            quiet=arguments.quiet)


def _ExtractCmdArgParser(subparser):
  subparser.description = 'Extract packages from tar directory.'
  subparser.add_argument(
    '--skip-missing', dest='extract__skip_missing',
    action='store_true', default=False,
    help='Skip missing archive files when extracting rather than erroring out.')


def _DoExtractCmd(arguments):
  ExtractPackageTargets(
      arguments.package_target_packages,
      arguments.tar_dir,
      arguments.dest_dir,
      skip_missing=arguments.extract__skip_missing,
      quiet=arguments.quiet)


def _UploadCmdArgParser(subparser):
  subparser.description = 'Upload a package file.'
  subparser.add_argument(
    '--upload-package', metavar='NAME', dest='upload__package', required=True,
    help='Package to upload.')
  subparser.add_argument(
    '--revision', metavar='NUM', dest='upload__revision', required=True,
    help='SVN Revision of the package to upload.')
  subparser.add_argument(
    '--package-file', metavar='FILE', dest='upload__file',
    default=None,
    help='Use custom package file instead of standard package file found'
         ' in the tar directory.')
  subparser.add_argument(
    '--skip-missing', dest='upload__skip_missing',
    action='store_true', default=False,
    help='Skip missing archive files when uploading package archives.')


def _DoUploadCmd(arguments):
  package_target_packages = GetPackageTargetPackages(
      arguments.upload__package,
      arguments.package_target_packages
  )
  if not package_target_packages:
    raise error.Error('Unknown package: %s.' % arguments.upload__package
                + ' Did you forget to add "$PACKAGE_TARGET/"?')

  for package_target, package_name in package_target_packages:
    UploadPackage(
        arguments.gsd_store,
        arguments.upload__revision,
        arguments.tar_dir,
        package_target,
        package_name,
        arguments.packages_desc.IsSharedPackage(package_name),
        annotate=arguments.annotate,
        skip_missing=arguments.upload__skip_missing,
        custom_package_file=arguments.upload__file
    )


def _SyncCmdArgParser(subparser):
  subparser.description = 'Download package archives to the tar directory.'
  subparser.add_argument(
    '--revision', metavar='NUM', dest='sync__revision',
    default=None,
    help='SVN Revision of the packages to download.')
  subparser.add_argument(
    '--include-logs', dest='sync__include_logs',
    action='store_true', default=False,
    help='Also download logs next to each archive if available.')
  subparser.add_argument(
    '-x', '--extract', dest='sync__extract',
    action='store_true', default=False,
    help='Extract package archives after they have been downloaded.')


def _DoSyncCmd(arguments):
  for package_target, package_name in arguments.package_target_packages:
    if arguments.sync__revision is None:
      # When the sync revision number is not specified, use the set
      # revision number found in the revision directory.
      revision_file = package_locations.GetRevisionFile(
          arguments.revisions_dir,
          package_name)
      revision_desc = revision_info.RevisionInfo(
          arguments.packages_desc,
          revision_file)
      package_desc = revision_desc.GetPackageInfo(package_target)
      revision_num = revision_desc.GetRevisionNumber()
    else:
      # When the sync revision number is specified, find the package to
      # download remotely using the revision.
      revision_num = arguments.sync__revision
      remote_package_key = package_locations.GetRemotePackageKey(
          arguments.packages_desc.IsSharedPackage(package_name),
          arguments.sync__revision,
          package_target,
          package_name)
      with pynacl.working_directory.TemporaryWorkingDirectory() as work_dir:
        temp_package_file = os.path.join(
            work_dir,
            os.path.basename(remote_package_key) + TEMP_SUFFIX)

        package_info.DownloadPackageInfoFiles(
            temp_package_file,
            remote_package_key,
            downloader=arguments.gsd_store.GetFile)

        package_desc = package_info.PackageInfo(temp_package_file)

    DownloadPackageArchives(
        arguments.tar_dir,
        package_target,
        package_name,
        package_desc,
        revision_num=revision_num,
        include_logs=arguments.sync__include_logs)

  CleanTempFiles(arguments.tar_dir)

  if arguments.sync__extract:
    ExtractPackageTargets(
        arguments.package_target_packages,
        arguments.tar_dir,
        arguments.dest_dir,
        quiet=arguments.quiet)


def _SetRevisionCmdArgParser(subparser):
  subparser.description = 'Specify the revision of a package.'
  subparser.add_argument(
    '--revision-package', metavar='NAME', dest='setrevision__package',
    required=True,
    help='Package name to set revision of.')
  subparser.add_argument(
    '--revision', metavar='NUM', dest='setrevision__revision',
    type=int, required=True,
    help='SVN Revision of the package to set.')


def _DoSetRevisionCmd(arguments):
  package_name = arguments.setrevision__package
  revision_num = arguments.setrevision__revision

  revision_desc = revision_info.RevisionInfo(arguments.packages_desc)
  revision_desc.SetRevisionNumber(revision_num)

  custom_package_targets = GetPackageTargetPackages(package_name, [])
  if not custom_package_targets:
    package_targets = arguments.packages_desc.GetPackageTargetsForPackage(
        package_name)
  else:
    package_targets = [target[0] for target in custom_package_targets]
    first_target = custom_package_targets[0]
    package_name = first_target[1]

  with pynacl.working_directory.TemporaryWorkingDirectory() as work_dir:
    for package_target in package_targets:
      remote_package_key = package_locations.GetRemotePackageKey(
          arguments.packages_desc.IsSharedPackage(package_name),
          revision_num,
          package_target,
          package_name)

      temp_package_file = os.path.join(
          work_dir,
          os.path.basename(remote_package_key) + TEMP_SUFFIX)

      package_info.DownloadPackageInfoFiles(
          temp_package_file,
          remote_package_key,
          downloader=arguments.gsd_store.GetFile)

      package_desc = package_info.PackageInfo(temp_package_file)

      logging.info('Setting %s:%s to revision %s',
                   package_target, package_name, revision_num)
      revision_desc.SetTargetRevision(
          package_name,
          package_target,
          package_desc)

  revision_file = package_locations.GetRevisionFile(
      arguments.revisions_dir,
      package_name)
  pynacl.file_tools.MakeParentDirectoryIfAbsent(revision_file)
  revision_desc.SaveRevisionFile(revision_file)

  CleanTempFiles(arguments.revisions_dir)


def _GetRevisionCmdArgParser(subparser):
  subparser.description = 'Get the revision of a package.'
  subparser.add_argument(
    '--revision-package', metavar='NAME', dest='getrevision__package',
    required=True,
    help='Package name to get revision of.')


def _DoGetRevisionCmd(arguments):
  package_name = arguments.getrevision__package

  custom_package_targets = GetPackageTargetPackages(package_name, [])
  if custom_package_targets:
    custom_target, package_name = custom_package_targets[0]

  revision_file = package_locations.GetRevisionFile(arguments.revisions_dir,
                                                    package_name)

  if not os.path.isfile(revision_file):
    raise error.Error('No revision set for package: %s.' % package_name)

  revision_desc = revision_info.RevisionInfo(arguments.packages_desc,
                                             revision_file)
  print revision_desc.GetRevisionNumber()


def _FillEmptyTarsParser(subparser):
  subparser.description = 'Fill missing archives with empty ones in a package.'
  subparser.add_argument(
    '--fill-package', metavar='NAME', dest='fillemptytars_package',
    required=True,
    help='Package name to fill empty archives of.')


def _DoFillEmptyTarsCmd(arguments):
  package_target_packages = GetPackageTargetPackages(
      arguments.fillemptytars_package,
      arguments.package_target_packages
  )
  if not package_target_packages:
    raise error.Error('Unknown package: %s.' % arguments.fillemptytars_package
                + ' Did you forget to add "$PACKAGE_TARGET/"?')

  for package_target, package_name in package_target_packages:
    package_path = package_locations.GetLocalPackageFile(arguments.tar_dir,
                                                         package_target,
                                                         package_name)

    package_desc = package_info.PackageInfo(package_path, skip_missing=True)
    output_package_desc = package_info.PackageInfo()
    for archive in package_desc.GetArchiveList():
      # If archive does not exist, fill it with an empty one.
      archive_data = archive.GetArchiveData()
      if archive_data.hash:
        output_package_desc.AppendArchive(archive)
      else:
        logging.info('Filling missing archive: %s.', archive_data.name)
        if (archive_data.name.endswith('.tar.gz') or
            archive_data.name.endswith('.tgz')):
          mode = 'w:gz'
        elif archive_data.name.endswith('.bz2'):
          mode = 'w:bz2'
        elif archive_data.name.endswith('.tar'):
          mode = 'w:'
        else:
          raise error.Error('Unknown archive type: %s.' % archive_data.name)

        archive_file = package_locations.GetLocalPackageArchiveFile(
            arguments.tar_dir,
            package_target,
            package_name,
            archive_data.name
            )

        tar_file = cygtar.CygTar(archive_file, mode)
        tar_file.Close()
        tar_hash = archive_info.GetArchiveHash(archive_file)

        empty_archive = archive_info.ArchiveInfo(name=archive_data.name,
                                                 hash=tar_hash)
        output_package_desc.AppendArchive(empty_archive)

    output_package_desc.SavePackageFile(package_path)


def _RecalcRevisions(subparser):
  subparser.description = 'Recalculates hashes for files in revision directory.'


def _DoRecalcRevisions(arguments):
  for json_file in os.listdir(arguments.revisions_dir):
    if json_file.endswith('.json'):
      revision_file = os.path.join(arguments.revisions_dir, json_file)
      revision_desc = revision_info.RevisionInfo(arguments.packages_desc)
      revision_desc.LoadRevisionFile(revision_file, skip_hash_verify=True)
      revision_desc.SaveRevisionFile(revision_file)


CommandFuncs = collections.namedtuple(
    'CommandFuncs',
    ['parse_func', 'do_cmd_func'])


COMMANDS = {
    'list': CommandFuncs(_ListCmdArgParser, _DoListCmd),
    'archive': CommandFuncs(_ArchiveCmdArgParser, _DoArchiveCmd),
    'extract': CommandFuncs(_ExtractCmdArgParser, _DoExtractCmd),
    'upload': CommandFuncs(_UploadCmdArgParser, _DoUploadCmd),
    'sync': CommandFuncs(_SyncCmdArgParser, _DoSyncCmd),
    'setrevision': CommandFuncs(_SetRevisionCmdArgParser, _DoSetRevisionCmd),
    'getrevision': CommandFuncs(_GetRevisionCmdArgParser, _DoGetRevisionCmd),
    'fillemptytars': CommandFuncs(_FillEmptyTarsParser, _DoFillEmptyTarsCmd),
    'recalcrevisions': CommandFuncs(_RecalcRevisions, _DoRecalcRevisions),
}


def ParseArgs(args):
  parser = argparse.ArgumentParser()

  host_platform = pynacl.platform.GetOS()
  host_arch = pynacl.platform.GetArch3264()

  # List out global options for all commands.
  parser.add_argument(
    '-v', '--verbose', dest='verbose',
    action='store_true', default=False,
    help='Verbose output')
  parser.add_argument(
    '-q', '--quiet', dest='quiet',
    action='store_true', default=False,
    help='Quiet output')
  parser.add_argument(
    '--platform', dest='host_platform',
    default=host_platform,
    help='Custom platform other than the current (%s).' % host_platform)
  parser.add_argument(
    '--arch', dest='host_arch',
    default=host_arch,
    help='Custom architecture other than the current (%s).' % host_arch)
  parser.add_argument(
    '--package-targets', dest='package_targets',
    default=None,
    help='Custom package targets specifed as comma separated names. Defaults'
         ' to package targets defined for host platform and architecture inside'
         ' of the packages json file.')
  parser.add_argument(
    '--packages', dest='packages',
    default=None,
    help='Custom packages specified as comma separated package names. Custom'
         ' packages not defined by the packages json file must be prefixed by'
         ' the package_target directory (IE. $PACKAGE_TARGET/$PACKAGE).')
  parser.add_argument(
    '--append', metavar='PACKAGE', dest='append_packages',
    action='append', default=[],
    help='Append extra package to current list of packages.')
  parser.add_argument(
    '--exclude', metavar='PACKAGE', dest='exclude_packages',
    action='append', default=[],
    help='Exclude package from current list of packages.')
  parser.add_argument(
    '--packages-json', dest='packages_json',
    default=DEFAULT_PACKAGES_JSON, type=argparse.FileType('rt'),
    help='Packages description file.'
         ' [Default: %s]' % DEFAULT_PACKAGES_JSON)
  parser.add_argument(
    '--revisions-dir', dest='revisions_dir',
    default=DEFAULT_REVISIONS_DIR,
    help='Revisions directory where packages revisions will be found.')
  parser.add_argument(
    '--dest-dir', dest='dest_dir',
    default=DEFAULT_DEST_DIR,
    help='Destination directory where all the packages will be extracted to.')
  parser.add_argument(
    '--tar-dir', dest='tar_dir',
    default=None,
    help='Directory for package archive files. Defaults to "$DEST-DIR/.tars".')
  parser.add_argument(
    '--annotate', dest='annotate',
    action='store_true', default=False,
    help='Print out build bot annotations.')
  parser.add_argument(
    '--cloud-bucket', dest='cloud_bucket',
    default=DEFAULT_CLOUD_BUCKET,
    help='Google storage cloud bucket name.'
         ' [Default: %s]' % DEFAULT_CLOUD_BUCKET)

  # Add subparsers for all commands. These are flags for specific commands,
  # IE. [options] command [command-options]
  command_parser = parser.add_subparsers(title='command', dest='command')
  for command, cmd_funcs in COMMANDS.iteritems():
    sub_parser = command_parser.add_parser(command)
    cmd_funcs.parse_func(sub_parser)

  arguments = parser.parse_args(args)
  pynacl.log_tools.SetupLogging(
      verbose=arguments.verbose, quiet=arguments.quiet)
  if arguments.tar_dir is None:
    arguments.tar_dir = os.path.join(arguments.dest_dir, '.tars')

  # Parse the package description up front and store it into the arguments
  # object. Almost all the commands need to use this information.
  packages_desc = packages_info.PackagesInfo(arguments.packages_json)
  arguments.packages_desc = packages_desc

  # Based on the host platform and host architecture, we can determine the set
  # of package targets used from the packages description. Minimize platform
  # and architecture errors by standardizing the names using pynacl.platform.
  if arguments.package_targets is None:
    package_targets = packages_desc.GetPackageTargets(
        pynacl.platform.GetOS(arguments.host_platform),
        pynacl.platform.GetArch3264(arguments.host_arch))
  else:
    package_targets = arguments.package_targets.split(',')

  # If the packages argument were not set, use the default list of packages
  # for each package target.
  packages_set = set()
  if arguments.packages is None:
    for package_target in package_targets:
      packages = packages_desc.GetPackages(package_target)
      if packages is None:
        raise error.Error('No packages defined for Package Target: %s.' %
                          package_target)
      packages_set.update(packages)
  else:
    packages_set.update(arguments.packages.split(','))

  # Append/exclude any extra packages that were specified.
  packages_set.update(arguments.append_packages)
  packages_set.difference_update(arguments.exclude_packages)

  # Build a dictionary that organizes packages to their respective package
  # targets. Packages may exist in multiple package targets so we will have
  # to have the key be package and value be a list of package targets.
  package_targets_dict = collections.defaultdict(list)
  for package_target in package_targets:
    for package in packages_desc.GetPackages(package_target):
      package_targets_dict[package].append(package_target)

  # Use the list of packages to determine the set of package target packages
  # we are operating on, custom package targets will have the package target
  # inside of the name of the package name (see help for "--packages" argument).
  # The package_target_packages is a list of tuples (package_target, package),
  # for every package along with the associated package target.
  package_target_packages = []
  for package in sorted(packages_set):
    package_targets = package_targets_dict.get(package, None)
    if package_targets is None:
      custom_package_targets = GetPackageTargetPackages(package, [])
      if not custom_package_targets:
        raise error.Error('Invalid custom package: "%s".'
                          ' Expected $PACKAGE_TARGET/$PACKAGE' % package)
      package_target_packages.extend(custom_package_targets)
    else:
      for package_target in package_targets:
        package_target_packages.append((package_target, package))

  arguments.package_target_packages = package_target_packages

  # Create a GSD Storage object for those who need it.
  cloud_bucket = arguments.cloud_bucket
  gsd_store = pynacl.gsd_storage.GSDStorage(cloud_bucket, [cloud_bucket])
  arguments.gsd_store = gsd_store

  return arguments


def main(args):
  try:
    arguments = ParseArgs(args)
    return COMMANDS[arguments.command].do_cmd_func(arguments)
  except error.Error as e:
    sys.stderr.write('package_version: ' + str(e) + '\n')
    return 1


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
