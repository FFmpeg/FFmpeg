#!/bin/bash
make
python setup.py py2app -A
cp ffplay dist/labeler.app/Contents/Resources/
open dist
