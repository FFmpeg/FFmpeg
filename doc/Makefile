all: ffmpeg-doc.html faq.html ffserver-doc.html

%.html: %.texi
	texi2html -monolithic -number $<
