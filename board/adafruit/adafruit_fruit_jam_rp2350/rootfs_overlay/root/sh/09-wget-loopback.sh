#!/bin/sh
set -e

fruitjam-services httpd
echo "writing /tmp/fruitjam-index.html with target-side wget"
wget -O /tmp/fruitjam-index.html http://127.0.0.1/index.html
echo "writing /tmp/fruitjam-playground.html with target-side wget"
exec wget -O /tmp/fruitjam-playground.html http://127.0.0.1/playground
