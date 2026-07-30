import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";

const server = await readFile("components/pf_web/health_server.cpp", "utf8");
const serializer = await readFile(
  "components/pf_web/include/pf_web/image_list_serializer.hpp",
  "utf8",
);

assert.ok(server.includes('uri = "/api/v1/images/*"'));
assert.ok(server.includes("httpd_uri_match_wildcard"));
assert.ok(server.includes("image_download_handler"));
assert.ok(server.includes("current_access_context(request)"));
assert.ok(server.includes("application/vnd.paperframe.pfr1"));
assert.ok(server.includes("serialize_image_content_disposition"));
assert.ok(server.includes("httpd_resp_send_chunk"));
assert.ok(server.includes("nullptr"));
assert.ok(server.includes("httpd_req_async_handler_begin"));
assert.ok(server.includes("xQueueSend(image_download_queue"));
assert.ok(server.includes("xTaskCreateStatic"));
assert.ok(server.includes("xQueueSpacesAvailable(image_download_queue)"));
assert.ok(server.includes("queued.content_disposition"));
assert.ok(server.includes("download_busy"));
assert.ok(!server.includes('httpd_resp_set_hdr(request, "Content-Length"'));
assert.ok(serializer.includes("attachment; filename=\\\""));
console.log("image_download_contract: 15 tests passed");
