import sys, os
body = sys.stdin.read()
print("Content-Type: text/html")
print()
print("<h1>CGI POST</h1>")
print("<p>Method:", os.environ.get("REQUEST_METHOD"), "</p>")
print("<p>Body received:", body, "</p>")
