import sys
body = sys.stdin.read()
name = body.split("=")[1] if "=" in body else "stranger"
print("Content-Type: text/html")
print()
print("<h1>Hello,", name, "!</h1>")
print("<a href=\"/form.html\">back</a>")
