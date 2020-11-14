plugin_sources = [
  'src/gstdabplusparse.c',
  'plugin.c'
  ]

plugin_c_args = ['-DHAVE_CONFIG_H']

library_core_name = 'dabplugin'
library_full_name = 'gst' + library_core_name

gstdabplugin = library(library_full_name,
  plugin_sources,
  c_args: plugin_c_args,
  dependencies : [gst_dep, gstbase_dep, gstpbutils_dep],
  install : true,
  install_dir : plugins_install_dir,
)

cdata = configuration_data()
cdata.set_quoted('PACKAGE', meson.project_name())
cdata.set_quoted('PACKAGE_VERSION', meson.project_version())
cdata.set_quoted('GST_LICENSE', meson.project_license())
cdata.set_quoted('GST_API_VERSION', api_version)
cdata.set_quoted('GST_PACKAGE_NAME', 'GStreamer DAB plugins')
cdata.set_quoted('GST_PACKAGE_ORIGIN', 'https://github.com/lukasz-wiecaszek/gst-plugins-dab')
cdata.set('GST_PACKAGE_LIBRARY', library_core_name)
configure_file(output : 'config.h', configuration : cdata)
