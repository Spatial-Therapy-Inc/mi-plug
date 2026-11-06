// The config page's import (main/page.html, between MIIO_IMPORT_BEGIN and _END) against an extractor -o file.
// Run: node test/test_page.mjs (test/run.sh does when node is there).
import { readFileSync } from "node:fs";
import assert from "node:assert/strict";

const page = readFileSync(new URL("../main/page.html", import.meta.url), "utf8");
const code = page.split("// MIIO_IMPORT_BEGIN")[1].split("// MIIO_IMPORT_END")[0];
const { parseExport, uniqueNames } = new Function(code + "\nreturn { parseExport, uniqueNames };")();

const T = "0123456789abcdef0123456789ABCDEF";
const file = [{
  server: "cn",
  homes: [{
    home_id: 1, home_owner: 2,
    devices: [
      { name: "客厅插座", did: "615005517", localip: "192.168.31.20", token: T, model: "cuco.plug.v3" },
      { name: "空间疗愈电视", did: "mitv.6ca3", localip: "192.168.31.117", token: "463646535176454741626763426d4c6d", model: "xiaomi.tv.v1" },
      { name: "路由器", did: "miwifi.094b", token: "QSh/6mmQNkwxp+OwF/iznuaiphEtTkLpsOttYa+C5Ks=", model: "xiaomi.router.r4av2" },
      { name: "温湿度计", did: "blt.3.abc", localip: "", token: "", model: "miaomiaoce.sensor_ht.t2" },
      { name: "台灯", did: "4567", localip: "192.168.31.21", token: T, model: "yeelink.light.lamp4" },
      { name: "台灯", did: "4568", localip: "192.168.31.22", token: T, model: "dmaker.fan.p5" },
      { name: "客厅插座", did: "615005517", localip: "192.168.31.20", token: T, model: "cuco.plug.v3" },
    ],
  }],
}, { server: "de", homes: [] }];

const got = parseExport(file);
assert.deepEqual(got.map((d) => d.name), ["客厅插座", "空间疗愈电视", "台灯", "台灯"]);
assert.deepEqual(got.map((d) => d.likely), [true, false, true, true]);
assert.equal(got[0].token, T.toLowerCase());
assert.equal(got[0].ip, "192.168.31.20");

const named = uniqueNames(got, ["客厅插座"]);
assert.deepEqual(named.map((d) => d.name), ["客厅插座 2", "空间疗愈电视", "台灯", "台灯 2"]);

assert.deepEqual(parseExport({ homes: [{ devices: [file[0].homes[0].devices[0]] }] }).length, 1);
assert.deepEqual(parseExport([file[0].homes[0].devices[4]]).length, 1);
assert.deepEqual(parseExport("nonsense"), []);
assert.deepEqual(parseExport(null), []);
console.log("config page import: ok");
