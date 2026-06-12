#!/bin/sh
set -e

fruitjam-services httpd
echo "writing /tmp/fruitjam-index.html with target-side wget"
exec wget -O /tmp/fruitjam-index.html http://127.0.0.1/index.html
