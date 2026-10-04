#!/usr/bin/env python3
"""Test ACS for the host test of icwmp_tr098d (tests/host).

Answers Inform and TransferComplete, runs a fixed list of RPCs per session
(PLAN, one RPC of BAD with --plan, or one SPV with --set), ends the session with 204 and sends
a Connection Request (digest cr/crpass) so the next session starts, until
--sessions sessions.  One line per session on stdout, "DONE ..." at the end."""
import argparse, re, sys, threading, time, urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

NS = ('xmlns:soap="http://schemas.xmlsoap.org/soap/envelope/" '
      'xmlns:soap_enc="http://schemas.xmlsoap.org/soap/encoding/" '
      'xmlns:xsd="http://www.w3.org/2001/XMLSchema" '
      'xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" '
      'xmlns:cwmp="urn:dslforum-org:cwmp-1-2"')

def env(body, rid="1"):
    return ('<?xml version="1.0" encoding="UTF-8"?><soap:Envelope %s><soap:Header>'
            '<cwmp:ID soap:mustUnderstand="1">%s</cwmp:ID></soap:Header><soap:Body>%s'
            '</soap:Body></soap:Envelope>' % (NS, rid, body))

def gpv(*names):
    s = "".join("<string>%s</string>" % n for n in names)
    return ('<cwmp:GetParameterValues><ParameterNames soap_enc:arrayType="xsd:string[%d]">%s'
            '</ParameterNames></cwmp:GetParameterValues>' % (len(names), s))

def gpn(path, nl):
    return ('<cwmp:GetParameterNames><ParameterPath>%s</ParameterPath><NextLevel>%s</NextLevel>'
            '</cwmp:GetParameterNames>' % (path, nl))

def spv(key, *pv):
    s = "".join('<ParameterValueStruct><Name>%s</Name><Value xsi:type="xsd:string">%s</Value>'
                '</ParameterValueStruct>' % p for p in pv)
    return ('<cwmp:SetParameterValues><ParameterList soap_enc:arrayType="cwmp:ParameterValueStruct[%d]">'
            '%s</ParameterList><ParameterKey>%s</ParameterKey></cwmp:SetParameterValues>' % (len(pv), s, key))

def spa(name, notif):
    return ('<cwmp:SetParameterAttributes><ParameterList soap_enc:arrayType="cwmp:SetParameterAttributesStruct[1]">'
            '<SetParameterAttributesStruct><Name>%s</Name><NotificationChange>1</NotificationChange>'
            '<Notification>%d</Notification><AccessListChange>0</AccessListChange><AccessList></AccessList>'
            '</SetParameterAttributesStruct></ParameterList></cwmp:SetParameterAttributes>' % (name, notif))

def gpa(name):
    return ('<cwmp:GetParameterAttributes><ParameterNames soap_enc:arrayType="xsd:string[1]">'
            '<string>%s</string></ParameterNames></cwmp:GetParameterAttributes>' % name)

def addobj(path):
    return '<cwmp:AddObject><ObjectName>%s</ObjectName><ParameterKey>k</ParameterKey></cwmp:AddObject>' % path

def delobj(path):
    return '<cwmp:DeleteObject><ObjectName>%s</ObjectName><ParameterKey>k</ParameterKey></cwmp:DeleteObject>' % path

IGD = "InternetGatewayDevice."
BAD = {
    "bad_download": '<cwmp:Download><CommandKey>x</CommandKey><FileType></FileType><URL>http://127.0.0.1:1/f</URL>'
                    '<Username></Username><Password></Password><FileSize>1</FileSize><TargetFileName></TargetFileName>'
                    '<DelaySeconds>0</DelaySeconds><SuccessURL></SuccessURL><FailureURL></FailureURL></cwmp:Download>',
    "bad_upload": '<cwmp:Upload><CommandKey>x</CommandKey><FileType></FileType><URL>http://127.0.0.1:1/f</URL>'
                  '<Username></Username><Password></Password><DelaySeconds>0</DelaySeconds></cwmp:Upload>',
    "schedule_download_ok": '<cwmp:ScheduleDownload><CommandKey>x</CommandKey><FileType>1 Firmware Upgrade Image</FileType>'
                  '<URL>http://127.0.0.1:1/f</URL><Username></Username><Password></Password><FileSize>1</FileSize>'
                  '<TargetFileName></TargetFileName><TimeWindowList soap_enc:arrayType="cwmp:TimeWindowStruct[1]"><TimeWindowStruct>'
                  '<WindowStart>3600</WindowStart><WindowEnd>7200</WindowEnd><WindowMode>1 At Any Time</WindowMode>'
                  '<UserMessage></UserMessage><MaxRetries>0</MaxRetries></TimeWindowStruct></TimeWindowList></cwmp:ScheduleDownload>',
    "schedule_download_3win": '<cwmp:ScheduleDownload><CommandKey>x</CommandKey><FileType>1 Firmware Upgrade Image</FileType>'
                  '<URL>http://127.0.0.1:1/f</URL><Username></Username><Password></Password><FileSize>1</FileSize>'
                  '<TargetFileName></TargetFileName><TimeWindowList soap_enc:arrayType="cwmp:TimeWindowStruct[3]">'
                  + "".join(['<TimeWindowStruct><WindowStart>3600</WindowStart><WindowEnd>7200</WindowEnd><WindowMode>1 At Any Time</WindowMode><UserMessage></UserMessage><MaxRetries>0</MaxRetries></TimeWindowStruct>','<TimeWindowStruct><WindowStart>7300</WindowStart><WindowEnd>9000</WindowEnd><WindowMode>1 At Any Time</WindowMode><UserMessage></UserMessage><MaxRetries>0</MaxRetries></TimeWindowStruct>','<TimeWindowStruct><WindowStart>9100</WindowStart><WindowEnd>99999</WindowEnd><WindowMode>1 At Any Time</WindowMode><UserMessage></UserMessage><MaxRetries>0</MaxRetries></TimeWindowStruct>']) +
                  '</TimeWindowList></cwmp:ScheduleDownload>',
    "bad_schedule_download": '<cwmp:ScheduleDownload><CommandKey>x</CommandKey><FileType></FileType><URL>http://127.0.0.1:1/f</URL>'
                  '<Username></Username><Password></Password><FileSize>1</FileSize><TargetFileName></TargetFileName>'
                  '<TimeWindowList soap_enc:arrayType="cwmp:TimeWindowStruct[1]"><TimeWindowStruct><WindowStart>0</WindowStart>'
                  '<WindowEnd>60</WindowEnd><WindowMode></WindowMode><UserMessage></UserMessage><MaxRetries>0</MaxRetries>'
                  '</TimeWindowStruct></TimeWindowList></cwmp:ScheduleDownload>',
}
PLAN = [
    gpv(IGD),
    gpn(IGD, "false"),
    gpn(IGD, "true"),
    gpv(IGD + "WANDevice.", IGD + "DeviceInfo.", IGD + "LANDevice."),
    gpv(IGD + "DeviceInfo.SoftwareVersion", IGD + "Firewall.", IGD + "Nope.X"),
    spv("k1", (IGD + "X_AIS_Telnet.Enable", "1"), (IGD + "Firewall.Config", "x")),
    spv("k2", (IGD + "ManagementServer.PeriodicInformInterval", "86400")),
    spv("k3", (IGD + "DeviceInfo.Nope", "1")),
    spa(IGD + "Firewall.", 1),
    spa(IGD + "DeviceInfo.SoftwareVersion", 2),
    gpa(IGD + "DeviceInfo."),
    gpa(IGD + "Firewall."),
    addobj(IGD + "WANDevice.1.WANConnectionDevice.1.WANIPConnection."),
    delobj(IGD + "WANDevice.1.WANConnectionDevice.1.WANIPConnection.2."),
    "<cwmp:GetRPCMethods></cwmp:GetRPCMethods>",
]

DOWNLOAD = ('<cwmp:Download><CommandKey>dl</CommandKey><FileType>1 Firmware Upgrade Image</FileType>'
            '<URL>http://127.0.0.1:1/fw.bin</URL><Username></Username><Password></Password>'
            '<FileSize>10</FileSize><TargetFileName></TargetFileName><DelaySeconds>0</DelaySeconds>'
            '<SuccessURL></SuccessURL><FailureURL></FailureURL></cwmp:Download>')

state = {"queue": [], "sessions": 0, "rpcs": 0, "faults": 0, "start": time.time()}
lock = threading.Lock()
args = None

def cr():
    time.sleep(args.gap)
    pm = urllib.request.HTTPPasswordMgrWithDefaultRealm()
    pm.add_password(None, "http://127.0.0.1:%d/" % args.cr_port, "cr", "crpass")
    op = urllib.request.build_opener(urllib.request.HTTPDigestAuthHandler(pm))
    for _ in range(30):
        try:
            op.open("http://127.0.0.1:%d/" % args.cr_port, timeout=10).read()
            return
        except Exception:
            time.sleep(1)

class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, *a):
        pass
    def do_POST(self):
        n = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(n).decode("utf-8", "replace") if n else ""
        with lock:
            if "<cwmp:Inform>" in body or ":Inform>" in body and "InformResponse" not in body:
                state["sessions"] += 1
                state["queue"] = [r for r in PLAN if not args.readonly or not any(
                    k in r for k in ("SetParameterValues", "AddObject", "DeleteObject"))]
                if args.plan:
                    state["queue"] = [BAD[args.plan]]
                if args.set:
                    state["queue"] = [spv("kset", *(tuple(a.split("=", 1)) for a in args.set))]
                if args.download_every and state["sessions"] % args.download_every == 0:
                    state["queue"].append(DOWNLOAD)
                ev = ",".join(re.findall(r"<EventCode>([^<]*)</EventCode>", body))
                fw = body.count("<Name>InternetGatewayDevice.Firewall.")
                sys.stdout.write("session %d events=%s firewall_params=%d t=%.0fs\n" % (state["sessions"], ev, fw, time.time() - state["start"]))
                sys.stdout.flush()
                out = env("<cwmp:InformResponse><MaxEnvelopes>1</MaxEnvelopes></cwmp:InformResponse>")
            elif ":TransferComplete>" in body:
                state["transfers"] = state.get("transfers", 0) + 1
                out = env("<cwmp:TransferCompleteResponse></cwmp:TransferCompleteResponse>")
            else:
                if "Fault" in body:
                    state["faults"] += 1
                if state["queue"]:
                    state["rpcs"] += 1
                    out = env(state["queue"].pop(0), str(state["rpcs"]))
                else:
                    out = None
        if out is None:
            self.send_response(204)
            self.send_header("Content-Length", "0")
            self.end_headers()
            if state["sessions"] < args.sessions:
                threading.Thread(target=cr, daemon=True).start()
            else:
                sys.stdout.write("DONE sessions=%d rpcs=%d faults=%d transfers=%d\n" % (state["sessions"], state["rpcs"], state["faults"], state.get("transfers", 0)))
                sys.stdout.flush()
            return
        data = out.encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/xml; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

ap = argparse.ArgumentParser()
ap.add_argument("--port", type=int, default=18080)
ap.add_argument("--cr-port", type=int, default=7547)
ap.add_argument("--sessions", type=int, default=20)
ap.add_argument("--gap", type=float, default=0.5)
ap.add_argument("--download-every", type=int, default=0)
ap.add_argument("--readonly", action="store_true", help="no SPV/AddObject/DeleteObject")
ap.add_argument("--plan", default="", choices=[""] + sorted(BAD), help="only this one RPC per session")
ap.add_argument("--set", action="append", metavar="NAME=VALUE",
                help="only one SetParameterValues of these per session")
args = ap.parse_args()
ThreadingHTTPServer(("127.0.0.1", args.port), H).serve_forever()
