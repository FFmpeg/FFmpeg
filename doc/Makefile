all: ffmpeg-doc.html faq.html ffserver-doc.html hooks.html

%.html: %.texi Makefile
	texi2html -monolithic -number $<

clean:
	rm -f *.html
