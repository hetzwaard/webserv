import os, json, random, string

STORE = "/tmp/webserv_sessions.json"

def load():
    try:
        return json.load(open(STORE))
    except:
        return {}

def save(d):
    json.dump(d, open(STORE, "w"))

# read the incoming cookie
cookie = os.environ.get("HTTP_COOKIE", "")
sid = ""
for part in cookie.split(";"):
    part = part.strip()
    if part.startswith("session_id="):
        sid = part[len("session_id="):]

sessions = load()

new_session = False
if not sid or sid not in sessions:
    sid = "".join(random.choice(string.ascii_letters + string.digits) for _ in range(16))
    sessions[sid] = 0
    new_session = True

sessions[sid] += 1
save(sessions)

print("Content-Type: text/html")
if new_session:
    print("Set-Cookie: session_id=" + sid)
print()
print("<h1>Session Demo</h1>")
if new_session:
    print("<p>Welcome! A new session was created for you.</p>")
else:
    print("<p>Welcome back.</p>")
print("<p>Session ID: " + sid + "</p>")
print("<p>You have visited this page " + str(sessions[sid]) + " time(s).</p>")
