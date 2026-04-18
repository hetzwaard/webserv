#!/usr/bin/env python3
# Minimal CGI test script. Used by selcyilm to test CgiHandler in isolation.
#
# Behaviour:
#   - Prints CGI headers (Content-Type + Status) then a blank line, then body.
#   - Echoes selected env vars and the request body so you can verify that
#     CgiHandler set up the environment and piped stdin correctly.
#
# Invocation (from webserv once CGI is wired up):
#   GET /cgi-bin/hello.py?name=sedat
#   POST /cgi-bin/hello.py  with body "anything"

import os
import sys

print("Content-Type: text/plain\r")
print("Status: 200 OK\r")
print("\r")  # blank line separates headers from body

print("Hello from CGI!")
print("REQUEST_METHOD :", os.environ.get("REQUEST_METHOD", ""))
print("QUERY_STRING   :", os.environ.get("QUERY_STRING", ""))
print("CONTENT_LENGTH :", os.environ.get("CONTENT_LENGTH", ""))
print("CONTENT_TYPE   :", os.environ.get("CONTENT_TYPE", ""))
print("SCRIPT_NAME    :", os.environ.get("SCRIPT_NAME", ""))
print("PATH_INFO      :", os.environ.get("PATH_INFO", ""))
print("SERVER_NAME    :", os.environ.get("SERVER_NAME", ""))
print("SERVER_PORT    :", os.environ.get("SERVER_PORT", ""))
print()
print("--- body ---")
sys.stdout.flush()
# CGI reads until EOF on stdin
try:
	body = sys.stdin.read()
	print(body)
except Exception as e:
	print("stdin read error:", e)
