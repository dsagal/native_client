# Copyright (c) 2011 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'includes': [
    '../../../build/common.gypi',
  ],
  'targets': [
    {
      'target_name': 'ppapi_lib',
      'type': 'none',
      'variables': {
        'nlib_target': 'libnacl_file.a',
        'build_glibc': 1,
        'build_newlib': 1,
        'sources': ['nacl_file.cc']
      },
      'dependencies': [
        '<(DEPTH)/native_client/tools.gyp:prep_toolchain',
      ],
    },
  ],
}
