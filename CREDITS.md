# Credits

## Origin

TTCut-ng is based on TTCut by Ben-A. Altendorf / TriTime (2003-2010).

## Third-Party Assets

### Tux (Linux Penguin Mascot)

Tux was created in 1996 by **Larry Ewing** (`lewing@isc.tamu.edu`) using The GIMP.

Bundled as: `ui/pixmaps/Tux.svg` (kept in the repository for logo and artwork
work; the current application logo `ui/pixmaps/ttcut_logo_001.png` does not
contain Tux).

Original permission statement:

> Permission to use and/or modify this image is granted provided you
> acknowledge me `lewing@isc.tamu.edu` and The GIMP if someone asks.

## Dependencies

This project links against Qt5, libavformat/libavcodec/libavutil/libswscale
(FFmpeg), libmpv, and libmpeg2/libmpeg2convert, and uses libx264/libx265
through libavcodec. See `debian/copyright` and the upstream licenses of the
respective projects.
