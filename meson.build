project('gst-plugins-dab', 'c', version : '1.0', license : 'LGPL')

plugins_install_dir = join_paths(get_option('libdir'), 'gstreamer-1.0')

cc = meson.get_compiler('c')

api_version = '1.0'

# Mandatory GST deps
gst_dep = dependency('gstreamer-1.0', fallback : ['gstreamer', 'gst_dep'])
gstbase_dep = dependency('gstreamer-base-1.0', fallback : ['gstreamer', 'gst_base_dep'])
gstpbutils_dep = dependency('gstreamer-pbutils-1.0', fallback : ['gst-plugins-base', 'pbutils_dep'])

subdir('gst')
