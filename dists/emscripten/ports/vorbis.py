# Copyright 2015 The Emscripten Authors.  All rights reserved.
# Emscripten is available under two separate licenses, the MIT license and the
# University of Illinois/NCSA Open Source License.  Both these licenses can be
# found in the LICENSE file.

import logging
import os

TAG = 'v1.3.7'
HASH = '99bee75beb662f8520bbb18ad6dbf8590d30eb3a7360899f0ac4764ca72fe8013da37c9df21e525f9d2dc5632827d4b4cea558cbc938e7fbed0c41a29a7a2dc5'

deps = ['ogg']


def needed(settings):
  return settings.USE_VORBIS


def get(ports, settings, shared):
  ports.fetch_project('vorbis', 'https://github.com/xiph/vorbis/archive/' + TAG + '.zip', 'Vorbis-' + TAG, sha512hash=HASH)

  lib_name = ports.get_lib_name('libvorbis')
  lib_name_file = ports.get_lib_name('libvorbisfile')
  lib_name_enc = ports.get_lib_name('libvorbisenc')
  def create(library):
    def internal_create(final):
      logging.info('building port: vorbis')

      source_path = os.path.join(ports.get_dir(), 'vorbis', 'Vorbis-' + TAG)
      ports.install_header_dir(os.path.join(source_path, 'include', 'vorbis'))
      dest_path = os.path.join(shared.Cache.get_path('ports-builds'), 'vorbis')
 
      if library == lib_name:
        ports.build_port(os.path.join(source_path, 'lib'), final, 'vorbis',
                     flags=['-sUSE_OGG'],
                     exclude_files=['psytune', 'barkmel', 'tone', 'misc', 'vorbisfile.c', 'vorbisenc.c'])

      if library == lib_name_file:
        ports.build_port(os.path.join(source_path, 'lib', 'vorbisfile.c'), final, 'vorbis',
                     flags=['-sUSE_OGG'],)
      if library == lib_name_enc:
        ports.build_port(os.path.join(source_path, 'lib', 'vorbisenc.c'), final, 'vorbis',
                     flags=['-sUSE_OGG'],)

    return internal_create

  create.recreated_tree = False
  return [shared.Cache.get(lib_name, create(lib_name)), shared.Cache.get(lib_name_file, create(lib_name_file)), shared.Cache.get(lib_name_enc, create(lib_name_enc))]

def clear(ports, settings, shared):
  shared.cache.erase_lib('libvorbis.a')


def process_dependencies(settings):
  settings.USE_OGG = 1


def process_args(ports):
  return []


def show():
  return 'vorbis (-sUSE_VORBIS; zlib license)'
