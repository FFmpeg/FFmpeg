all: ffmpeg-doc.html faq.html ffserver-doc.html ffplay-doc.html hooks.html \
     ffmpeg.1 ffserver.1 ffplay.1

%.html: %.texi Makefile
	texi2html -monolithic -number $<

%.pod: %-doc.texi
	./texi2pod.pl $< $@

%.1: %.pod
	pod2man --section=1 --center=" " --release=" " $< > $@

clean:
	rm -f *.html *.pod *.1
