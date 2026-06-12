#!/bin/sh
set -e

fruitjam-services status

echo "== loopback CGI =="
fruitjam-services httpd
exec wget -O - http://127.0.0.1/cgi-bin/env.cgi
