Cropsicle
=========

Cropsicle is an implementation of the GrowCut algorithm described in the paper
"GrowCut - Interactive Multi-Label N-D Image Segmentation By Cellular Automata"
by Vladimir Vezhnevets and Vadim Konouchine [1].

It lets you remove the background from an image with minimal input, cutting
along irregular boundaries.

Build
-----

> gcc -g -O3 cropsicle.c $(pkg-config --libs --cflags libpng) -lm -pthread -o cropsicle

Run
---

This program supports 4-channel 8-bit-per-channel RGBA PNG images only. If
you have something else, you must convert it to the proper format first,
like this:

> convert image.jpg -channel rgba png32:image.png

Perform the GrowCut operation like this:

> cropsicle image.png overlay.png output.png

Image is the source image, overlay is an alpha-transparent overlay with
a few green pixels spread out over the foreground you want to keep
and red pixels over the background. The pixels don't have to be perfect red
and green as long as the corresponding red/green channels are dominant and
the pixels are not transparent.

Enjoy!

[1] http://graphicon.ru/oldgr/en/publications/text/gc2005vk.pdf
