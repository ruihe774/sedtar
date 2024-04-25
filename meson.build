project('sedtar', 'c',
        version : '0.1.0',
        default_options : ['warning_level=3', 'c_std=c11'])

libarchive = dependency('libarchive', version : '>=3.0.0')

executable('sedtar', 'sedtar.c', install : true, dependencies: libarchive, c_args : ['-D_DEFAULT_SOURCE', '-D_GNU_SOURCE'])
