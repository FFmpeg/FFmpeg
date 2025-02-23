#!/bin/bash

wget -q -O cvelist-our-html https://git.ffmpeg.org/gitweb/ffmpeg-web.git/blob_plain/HEAD:/src/security
wget -q -O cvelist-html     https://cve.mitre.org/cgi-bin/cvekey.cgi?keyword=ffmpeg

grep '>CVE-[0-2][90][0-9][0-9]-[0-9]*<' cvelist-html | sed 's/.*\(CVE-[0-2][90][0-9][0-9]-[0-9]*\)<.*/\1/g' |sort|uniq > cvelist
tr ', (' "\n" <cvelist-our-html | grep 'CVE-[0-2][90][0-9][0-9]-[0-9]*' | sed 's/.*\(CVE-[0-2][90][0-9][0-9]-[0-9]*\)[^0-9].*/\1/g' | sort | uniq  > cvelist-our
diff -u cvelist-our cvelist > cvelist.diff
diffstat cvelist.diff
echo please make sure CVE numbers added to our security page have been or will be backported
echo make sure the syntax on our security page is unchanged so it can be easily parsed by scripts
echo see cvelist.diff, delete cvelist  cvelist.diff  cvelist-html  cvelist-our  cvelist-our-html to cleanup
