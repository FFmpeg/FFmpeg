all: ffmpeg-doc.html faq.html ffserver-doc.html hooks.html

%.html: %.texi
	texi2html -monolithic -number $<
