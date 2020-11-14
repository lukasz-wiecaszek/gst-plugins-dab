# Gstreamer DAB (Digital Audio Broadcasting) plugin

This gstreamer DAB plugin is intended to store several gstreamer elements
responsible for DAB audio processing.
Currently this git module contains code for only one gstreamer element.
This is gstdabplusparse which is a parser for DAB+ audio stream as defined in
ETSI TS 102 563 "Digital Audio Broadcasting (DAB); DAB+ audio coding (MPEG HE-AACv2)".

## License

This code is provided under [LGPL] license.

## Dependencies

To build the project you will definitely need some of the gstreamer development packages.
As currently I am using Ubuntu these gstreamer packages could be installed by issuing
following commands:

    sudo apt install libgstreamer1.0-dev
    sudo apt install libgstreamer-plugins-base1.0-dev

## Usage

Configure and build the project using meson

    http://mesonbuild.com/Getting-meson.html

and ninja

	https://ninja-build.org/

Just run:

    meson builddir
    ninja -C builddir

See <https://mesonbuild.com/Quick-guide.html> on how to install the Meson
build system and ninja.

Once the plugin is built you can install system-wide it with

	sudo ninja -C builddir install

However, this will by default go into the `/usr/local` prefix where it won't
be picked up by a `GStreamer` installed from packages,
so you would need to set the `GST_PLUGIN_PATH` environment variable to include or
point to `/usr/local/lib/gstreamer-1.0/` for your plugin to be found by a
from-package `GStreamer`.

Alternatively, you will find your plugin binary in `builddir/gst/`
as `libgstdabplugin.so` or similar (the extension may vary), so you can also set
the `GST_PLUGIN_PATH` environment variable to the `builddir/gst/` directory
(best to specify an absolute path though).

Then, you can check if it has been built correctly with something similar to:

    GST_PLUGIN_PATH=~/projects/gstreamer/gst-plugins-dab/builddir/gst/ gst-inspect-1.0 libgstdabplugin.so

And finally verify whether the parser works as expected:

    GST_DEBUG=2,dabplusparse:5 GST_PLUGIN_PATH=~/projects/gstreamer/gst-plugins-dab/builddir/gst/  gst-launch-1.0 filesrc location=subchannel01.raw ! dabplusparse ! avdec_aac ! audioresample ! audioconvert ! autoaudiosink

You can also try other decoder:

    GST_DEBUG=2,dabplusparse:5 GST_PLUGIN_PATH=~/projects/gstreamer/gst-plugins-dab/builddir/gst/  gst-launch-1.0 filesrc location=subchannel02.raw ! dabplusparse ! faad ! audioresample ! audioconvert ! autoaudiosink

[LGPL]: http://www.opensource.org/licenses/lgpl-license.php or COPYING
