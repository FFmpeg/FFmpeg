all: ffmpeg-doc.html faq.html

%.html: %.texi
	texi2html -monolithic -number $<
