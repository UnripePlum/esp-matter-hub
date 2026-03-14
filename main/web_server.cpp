#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_timer.h>

#include "bridge_action.h"
#include "ir_engine.h"
#include "local_discovery.h"
#include "status_led.h"
#include "web_server.h"

static const char *TAG = "web_server";
static httpd_handle_t s_server = nullptr;
extern "C" esp_err_t app_open_commissioning_window(uint16_t timeout_seconds);

static esp_err_t register_uri_handler_checked(httpd_handle_t server, const httpd_uri_t *uri)
{
    esp_err_t err = httpd_register_uri_handler(server, uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register URI %s (method=%d): %s", uri->uri, static_cast<int>(uri->method),
                 esp_err_to_name(err));
    }
    return err;
}
static const char *kDashboardHtml =
    "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ESP Matter Hub</title><style>"
    "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;margin:24px;background:#f4f6f8;color:#111}"
    ".card{background:#fff;border-radius:12px;padding:16px;margin-bottom:16px;box-shadow:0 2px 10px rgba(0,0,0,.06)}"
    "button{padding:8px 12px;border:0;border-radius:8px;background:#0a84ff;color:#fff;cursor:pointer}"
    "button:disabled{background:#98a2b3;cursor:not-allowed}"
    "input,select{padding:8px;border:1px solid #cfd6dd;border-radius:8px;margin-right:8px}"
    "table{width:100%;border-collapse:collapse}th,td{padding:8px;border-bottom:1px solid #e9edf0;text-align:left}"
    ".row{display:flex;gap:8px;flex-wrap:wrap}.muted{color:#667085;font-size:13px}.pill{display:inline-block;padding:4px 10px;border-radius:999px;font-size:12px;font-weight:600}.pill.wait{background:#fff4ce;color:#7a5d00}.pill.ok{background:#dcfce7;color:#166534}.pill.err{background:#fee2e2;color:#991b1b}.pulse{animation:pulse .7s ease-in-out}@keyframes pulse{0%{transform:scale(1)}50%{transform:scale(1.06)}100%{transform:scale(1)}}</style></head><body>"
    "<h1>ESP Matter Hub</h1>"
    "<div class='card'><h3>Learn</h3><div class='row'>"
    "<input id='timeoutSec' type='number' value='15' min='1' step='1'/>"
    "<button id='startLearnBtn' onclick='startLearn()'>Start Learning</button>"
    "<input id='signalName' placeholder='signal name'/>"
    "<input id='deviceType' placeholder='device type (tv/ac/etc)'/>"
    "<button onclick='commitLearn()'>Commit Learned Signal</button>"
    "<button onclick='refreshStatus()'>Refresh Status</button>"
    "</div><p id='status' class='muted'>-</p><p id='captureHint' class='pill wait'>Idle</p></div>"
    "<div class='card'><h3>Signals</h3><button onclick='refreshSignals()'>Refresh Signals</button>"
    "<table><thead><tr><th>ID</th><th>Name</th><th>Type</th><th>Carrier</th><th>Repeat</th><th>Payload</th><th>Action</th></tr></thead><tbody id='signals'></tbody></table></div>"
    "<div class='card'><h3>Endpoint Slots (fixed: 0~7)</h3><button onclick='refreshSlots()'>Refresh Slots</button>"
    "<table><thead><tr><th>Slot</th><th>Role</th><th>Endpoint</th><th>Device ID</th><th>Display Name</th><th>On</th><th>Off</th><th>Level Up</th><th>Level Down</th></tr></thead><tbody id='slots'></tbody></table></div>"
    "<div class='card'><h3>Bind Signal to Slot</h3><div class='row'>"
    "<select id='slot'></select>"
    "<select id='onSignalId'><option value='0'>On: Unbind (0)</option></select>"
    "<select id='offSignalId'><option value='0'>Off: Unbind (0)</option></select>"
    "<select id='levelUpSignalId'><option value='0'>Level Up: Unbind (0)</option></select>"
    "<select id='levelDownSignalId'><option value='0'>Level Down: Unbind (0)</option></select>"
    "<button onclick='bindSignal()'>Bind</button></div><p id='bindResult' class='muted'>-</p><p id='bindSlotInfo' class='muted'>-</p></div>"
    "<div class='card'><h3>Devices (light)</h3><div class='row'>"
    "<input id='deviceName' placeholder='device name'/>"
    "<button onclick='registerDevice()'>Register Device</button>"
    "</div><p id='deviceResult' class='muted'>-</p><table><thead><tr><th>ID</th><th>Name</th><th>Type</th><th>Rename</th></tr></thead><tbody id='devices'></tbody></table></div>"
    "<div class='card'><h3>Endpoint Assignment</h3><div class='row'>"
    "<select id='assignSlot'></select>"
    "<select id='assignDeviceId'><option value='0'>Unassign (0)</option></select>"
    "<button onclick='assignDevice()'>Assign</button></div><p id='assignResult' class='muted'>-</p></div>"
    "<script>"
    "let lastSignalId=0,slots=[],signals=[],devices=[],lastCaptureKey='';"
    "async function j(url,opt){const r=await fetch(url,opt);return [r.status,await r.json().catch(()=>({}))];}"
    "async function refreshStatus(){const [s,d]=await j('/api/learn/status');"
    "const b=document.getElementById('startLearnBtn');if(b){b.disabled=(d.state==='in_progress');}"
    "lastSignalId=d.last_signal_id||0;const timeoutSec=((d.timeout_ms||0)/1000).toFixed(1);document.getElementById('status').textContent=`HTTP ${s} | state=${d.state} elapsed=${d.elapsed_ms}ms timeout=${timeoutSec}s last_signal_id=${lastSignalId} rx_source=${d.rx_source||0} captured_len=${d.captured_len||0} quality_score=${d.quality_score||0}`;"
    "const h=document.getElementById('captureHint');if(d.state==='in_progress'){h.className='pill wait';h.textContent='Listening... press remote button';}else if(d.state==='ready'&&(d.captured_len||0)>0){const key=`${d.rx_source||0}-${d.captured_len||0}-${d.quality_score||0}`;h.className='pill ok';h.textContent=`Captured! RX${d.rx_source||0}, len=${d.captured_len||0}`;if(lastCaptureKey!==key){h.classList.add('pulse');setTimeout(()=>h.classList.remove('pulse'),700);lastCaptureKey=key;}}else if(d.state==='failed'){h.className='pill err';h.textContent='Learning timeout. Try again';}else{h.className='pill wait';h.textContent='Idle';}}"
    "async function startLearn(){const timeout_s=Number(document.getElementById('timeoutSec').value)||15;"
    "const [s,d]=await j('/api/learn/start',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({timeout_s})});"
    "document.getElementById('status').textContent=`HTTP ${s} | ${JSON.stringify(d)}`;const b=document.getElementById('startLearnBtn');if(b){b.disabled=(s===200);}document.getElementById('captureHint').className='pill wait';document.getElementById('captureHint').textContent='Listening... press remote button';setTimeout(refreshStatus,300);}"
    "async function commitLearn(){const name=document.getElementById('signalName').value||'';const device_type=document.getElementById('deviceType').value||'';"
    "const [s,d]=await j('/api/learn/commit',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({name,device_type})});"
    "document.getElementById('status').textContent=`HTTP ${s} | ${JSON.stringify(d)}`;await refreshStatus();await refreshSignals();}"
    "function syncSignalSelect(){const ids=['onSignalId','offSignalId','levelUpSignalId','levelDownSignalId'];for(const id of ids){const sel=document.getElementById(id);if(!sel)continue;const cur=Number(sel.value)||0;sel.innerHTML='';const base=document.createElement('option');base.value='0';base.textContent=id==='onSignalId'?'On: Unbind (0)':id==='offSignalId'?'Off: Unbind (0)':id==='levelUpSignalId'?'Level Up: Unbind (0)':'Level Down: Unbind (0)';sel.appendChild(base);for(const x of signals){const o=document.createElement('option');o.value=String(x.signal_id);o.textContent=`${x.signal_id}: ${x.name||'-'} (${x.device_type||'-'})`;sel.appendChild(o);}sel.value=String(signals.some(x=>x.signal_id===cur)?cur:0);}const dsel=document.getElementById('assignDeviceId');if(dsel){const cur=Number(dsel.value)||0;dsel.innerHTML='';const z=document.createElement('option');z.value='0';z.textContent='Unassign (0)';dsel.appendChild(z);for(const d of devices){const o=document.createElement('option');o.value=String(d.device_id);o.textContent=`${d.device_id}: ${d.name}`;dsel.appendChild(o);}dsel.value=String(devices.some(d=>d.device_id===cur)?cur:0);}}"
    "function syncSlotSelects(){const ids=['slot','assignSlot'];for(const id of ids){const sel=document.getElementById(id);if(!sel)continue;const cur=Number(sel.value)||0;sel.innerHTML='';for(const x of slots){const o=document.createElement('option');o.value=String(x.slot_id);const n=(x.display_name&&x.display_name.length>0)?x.display_name:`Slot ${x.slot_id}`;o.textContent=`Slot ${x.slot_id} (${n})`;sel.appendChild(o);}if(slots.length===0){const o=document.createElement('option');o.value='0';o.textContent='Slot 0';sel.appendChild(o);}sel.value=String(slots.some(x=>x.slot_id===cur)?cur:(slots[0]?slots[0].slot_id:0));}updateBindSlotInfo();}"
    "function updateBindSlotInfo(){const slotId=Number(document.getElementById('slot').value)||0;const x=slots.find(s=>s.slot_id===slotId);const el=document.getElementById('bindSlotInfo');if(!el)return;if(!x){el.textContent='-';return;}el.textContent=`slot=${x.slot_id} endpoint=${x.endpoint_id} device=${x.device_id||0} name=${x.display_name||'-'}`;}"
    "async function refreshSignals(){const [s,d]=await j('/api/signals');const list=d.signals||[];signals=list;"
    "const t=document.getElementById('signals');t.innerHTML='';for(const x of list){const tr=document.createElement('tr');"
    "tr.innerHTML=`<td>${x.signal_id}</td><td>${x.name}</td><td>${x.device_type}</td><td>${x.carrier_hz}</td><td>${x.repeat}</td><td>${x.payload_len||0}</td><td><button onclick='deleteSignal(${x.signal_id})'>Delete</button></td>`;t.appendChild(tr);}syncSignalSelect();if(s!==200){document.getElementById('status').textContent=`signals error HTTP ${s}`;}}"
    "async function refreshSlots(){const [s,d]=await j('/api/slots');slots=d.slots||[];"
    "const t=document.getElementById('slots');t.innerHTML='';for(const x of slots){const tr=document.createElement('tr');"
    "tr.innerHTML=`<td>${x.slot_id}</td><td>${x.role||'-'}</td><td>${x.endpoint_id}</td><td>${x.device_id||0}</td><td>${x.display_name||'-'}</td><td>${x.on_signal_id}</td><td>${x.off_signal_id}</td><td>${x.level_up_signal_id}</td><td>${x.level_down_signal_id}</td>`;t.appendChild(tr);}syncSlotSelects();if(s!==200){document.getElementById('bindResult').textContent=`slots error HTTP ${s}`;}}"
    "async function refreshDevices(){const [s,d]=await j('/api/devices');devices=d.devices||[];const t=document.getElementById('devices');t.innerHTML='';for(const x of devices){const tr=document.createElement('tr');tr.innerHTML=`<td>${x.device_id}</td><td>${x.name}</td><td>${x.device_type}</td><td><input id='rename-${x.device_id}' placeholder='new name' style='max-width:140px'/><button onclick='renameDevice(${x.device_id})'>Rename</button></td>`;t.appendChild(tr);}if(s!==200){document.getElementById('deviceResult').textContent=`devices error HTTP ${s}`;}syncSignalSelect();}"
    "async function renameDevice(deviceId){const e=document.getElementById(`rename-${deviceId}`);const name=(e&&e.value)||'';const [s,d]=await j(`/api/devices/${deviceId}/rename`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({name})});document.getElementById('deviceResult').textContent=`HTTP ${s} | ${JSON.stringify(d)}`;await refreshDevices();await refreshSlots();}"
    "async function registerDevice(){const name=document.getElementById('deviceName').value||'';const [s,d]=await j('/api/devices/register',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({name,device_type:'light'})});document.getElementById('deviceResult').textContent=`HTTP ${s} | ${JSON.stringify(d)}`;await refreshDevices();}"
    "async function assignDevice(){const slotId=Number(document.getElementById('assignSlot').value)||0;const deviceId=Number(document.getElementById('assignDeviceId').value)||0;const [s,d]=await j(`/api/endpoints/${slotId}/assign`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({device_id:deviceId})});document.getElementById('assignResult').textContent=`HTTP ${s} | ${JSON.stringify(d)}`;await refreshSlots();}"
    "async function bindSignal(){const slotId=Number(document.getElementById('slot').value);"
    "const on=Number(document.getElementById('onSignalId').value)||0;const off=Number(document.getElementById('offSignalId').value)||0;const levelUp=Number(document.getElementById('levelUpSignalId').value)||0;const levelDown=Number(document.getElementById('levelDownSignalId').value)||0;"
    "const [s,d]=await j(`/api/slots/${slotId}/bind`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({on_signal_id:on,off_signal_id:off,level_up_signal_id:levelUp,level_down_signal_id:levelDown})});"
    "document.getElementById('bindResult').textContent=`HTTP ${s} | ${JSON.stringify(d)}`;await refreshSlots();}"
    "async function deleteSignal(signalId){if(!confirm(`Delete signal ${signalId}?`))return;const [s,d]=await j(`/api/signals/${signalId}`,{method:'DELETE'});document.getElementById('status').textContent=`HTTP ${s} | ${JSON.stringify(d)}`;await refreshSignals();await refreshDevices();await refreshSlots();}"
    "document.getElementById('slot').addEventListener('change',updateBindSlotInfo);refreshStatus();refreshSignals();refreshDevices();refreshSlots();setInterval(refreshStatus,1000);</script></body></html>";

static esp_err_t send_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t begin_json_stream(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    return ESP_OK;
}

static esp_err_t end_json_stream(httpd_req_t *req)
{
    return httpd_resp_sendstr_chunk(req, nullptr);
}

static esp_err_t dashboard_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req, kDashboardHtml);
}

static esp_err_t no_content_get_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, nullptr, 0);
}

static esp_err_t health_get_handler(httpd_req_t *req)
{
    size_t slot_count = 0;
    bridge_action_get_slots(&slot_count);

    const char *hostname = app_local_discovery_hostname();
    const char *fqdn = app_local_discovery_fqdn();
    const char *mdns_state = app_local_discovery_ready() ? "ready" : "disabled";
    const char *led_state = status_led_get_state_str();

    char body[320];
    snprintf(body, sizeof(body),
             "{\"status\":\"ok\",\"service\":\"esp-matter-hub\",\"slots\":%u,\"hostname\":\"%s\",\"fqdn\":\"%s\",\"mdns\":\"%s\",\"led_state\":\"%s\"}",
             static_cast<unsigned>(slot_count), hostname, fqdn, mdns_state, led_state);
    return send_json(req, body);
}

static esp_err_t slots_get_handler(httpd_req_t *req)
{
    size_t slot_count = 0;
    const bridge_slot_state_t *slots = bridge_action_get_slots(&slot_count);

    esp_err_t err = begin_json_stream(req);
    if (err != ESP_OK) {
        return err;
    }

    err = httpd_resp_sendstr_chunk(req, "{\"slots\":[");
    if (err != ESP_OK) {
        return err;
    }

    char item[320];
    for (size_t i = 0; i < slot_count; ++i) {
        const bridge_slot_state_t &slot = slots[i];
        const char *role = bridge_action_get_slot_role(slot.slot_id);
        snprintf(item, sizeof(item),
                 "%s{\"slot_id\":%u,\"role\":\"%s\",\"endpoint_id\":%u,\"device_id\":%lu,\"display_name\":\"%s\",\"on_signal_id\":%lu,\"off_signal_id\":%lu,\"level_up_signal_id\":%lu,\"level_down_signal_id\":%lu}",
                 (i == 0) ? "" : ",", slot.slot_id, role, slot.endpoint_id,
                 static_cast<unsigned long>(slot.assigned_device_id), slot.display_name,
                 static_cast<unsigned long>(slot.on_signal_id), static_cast<unsigned long>(slot.off_signal_id),
                 static_cast<unsigned long>(slot.level_up_signal_id), static_cast<unsigned long>(slot.level_down_signal_id));
        err = httpd_resp_sendstr_chunk(req, item);
        if (err != ESP_OK) {
            return err;
        }
    }

    err = httpd_resp_sendstr_chunk(req, "]}");
    if (err != ESP_OK) {
        return err;
    }
    return end_json_stream(req);
}

static bool parse_slot_id_from_uri(const char *uri, uint8_t *slot_id)
{
    const char *prefix = "/api/slots/";
    const char *suffix = "/bind";
    size_t prefix_len = strlen(prefix);
    size_t uri_len = strlen(uri);
    size_t suffix_len = strlen(suffix);

    if (!uri || !slot_id || uri_len <= (prefix_len + suffix_len)) {
        return false;
    }
    if (strncmp(uri, prefix, prefix_len) != 0) {
        return false;
    }
    if (strcmp(uri + uri_len - suffix_len, suffix) != 0) {
        return false;
    }

    const char *slot_str = uri + prefix_len;
    char *end = nullptr;
    long slot = strtol(slot_str, &end, 10);
    if (end == slot_str || strncmp(end, suffix, suffix_len) != 0 || slot < 0 || slot > 255) {
        return false;
    }

    *slot_id = static_cast<uint8_t>(slot);
    return true;
}

static bool parse_endpoint_slot_from_uri(const char *uri, uint8_t *slot_id)
{
    const char *prefix = "/api/endpoints/";
    const char *suffix = "/assign";
    size_t prefix_len = strlen(prefix);
    size_t uri_len = strlen(uri);
    size_t suffix_len = strlen(suffix);

    if (!uri || !slot_id || uri_len <= (prefix_len + suffix_len)) {
        return false;
    }
    if (strncmp(uri, prefix, prefix_len) != 0) {
        return false;
    }
    if (strcmp(uri + uri_len - suffix_len, suffix) != 0) {
        return false;
    }

    const char *slot_str = uri + prefix_len;
    char *end = nullptr;
    long slot = strtol(slot_str, &end, 10);
    if (end == slot_str || strncmp(end, suffix, suffix_len) != 0 || slot < 0 || slot > 255) {
        return false;
    }
    *slot_id = static_cast<uint8_t>(slot);
    return true;
}

static bool parse_device_id_from_uri(const char *uri, uint32_t *device_id)
{
    const char *prefix = "/api/devices/";
    const char *suffix = "/rename";
    size_t prefix_len = strlen(prefix);
    size_t uri_len = strlen(uri);
    size_t suffix_len = strlen(suffix);

    if (!uri || !device_id || uri_len <= (prefix_len + suffix_len)) {
        return false;
    }
    if (strncmp(uri, prefix, prefix_len) != 0) {
        return false;
    }
    if (strcmp(uri + uri_len - suffix_len, suffix) != 0) {
        return false;
    }

    const char *id_str = uri + prefix_len;
    char *end = nullptr;
    unsigned long id = strtoul(id_str, &end, 10);
    if (end == id_str || strncmp(end, suffix, suffix_len) != 0) {
        return false;
    }
    *device_id = static_cast<uint32_t>(id);
    return true;
}

static bool parse_signal_id_from_uri(const char *uri, uint32_t *signal_id)
{
    const char *prefix = "/api/signals/";
    size_t prefix_len = strlen(prefix);

    if (!uri || !signal_id || strncmp(uri, prefix, prefix_len) != 0) {
        return false;
    }

    const char *id_str = uri + prefix_len;
    if (*id_str == '\0') {
        return false;
    }

    char *end = nullptr;
    unsigned long id = strtoul(id_str, &end, 10);
    if (end == id_str || *end != '\0' || id == 0 || id > UINT32_MAX) {
        return false;
    }

    *signal_id = static_cast<uint32_t>(id);
    return true;
}

static bool parse_u32_field(const char *json, const char *field_name, uint32_t *out_value)
{
    if (!json || !field_name || !out_value) {
        return false;
    }

    const char *found = strstr(json, field_name);
    if (!found) {
        return false;
    }

    const char *colon = strchr(found, ':');
    if (!colon) {
        return false;
    }
    colon++;
    while (*colon == ' ' || *colon == '\t') {
        colon++;
    }

    char *end = nullptr;
    unsigned long value = strtoul(colon, &end, 10);
    if (end == colon) {
        return false;
    }

    *out_value = static_cast<uint32_t>(value);
    return true;
}

static bool parse_string_field(const char *json, const char *field_name, char *out_value, size_t out_size)
{
    if (!json || !field_name || !out_value || out_size == 0) {
        return false;
    }

    const char *found = strstr(json, field_name);
    if (!found) {
        return false;
    }

    const char *colon = strchr(found, ':');
    if (!colon) {
        return false;
    }
    const char *first_quote = strchr(colon, '"');
    if (!first_quote) {
        return false;
    }
    first_quote++;

    const char *second_quote = strchr(first_quote, '"');
    if (!second_quote || second_quote <= first_quote) {
        return false;
    }

    size_t len = static_cast<size_t>(second_quote - first_quote);
    if (len >= out_size) {
        len = out_size - 1;
    }
    memcpy(out_value, first_quote, len);
    out_value[len] = '\0';
    return true;
}

static bool query_scope_enabled(const char *scope, const char *target)
{
    if (!scope || strcmp(scope, "all") == 0) {
        return true;
    }
    return strcmp(scope, target) == 0;
}

static esp_err_t export_nvs_get_handler(httpd_req_t *req)
{
    char scope[24] = "all";
    char query[96];
    if (httpd_req_get_url_query_len(req) > 0 && httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char scope_param[24] = { 0 };
        if (httpd_query_key_value(query, "scope", scope_param, sizeof(scope_param)) == ESP_OK && scope_param[0] != '\0') {
            strlcpy(scope, scope_param, sizeof(scope));
        }
    }

    bool include_signals = query_scope_enabled(scope, "signals");
    bool include_bindings = query_scope_enabled(scope, "bindings");
    bool include_devices = query_scope_enabled(scope, "devices");

    if (!include_signals && !include_bindings && !include_devices) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid scope");
    }

    const ir_signal_record_t *signals = nullptr;
    size_t signal_count = 0;
    if (include_signals) {
        esp_err_t err = ir_engine_get_signals(&signals, &signal_count);
        if (err != ESP_OK) {
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "failed to read signals");
        }
    }

    static constexpr size_t kPayloadCap = 128;
    uint16_t *payload_pool = nullptr;
    uint8_t *payload_lens = nullptr;
    if (include_signals && signal_count > 0) {
        payload_pool = static_cast<uint16_t *>(malloc(signal_count * kPayloadCap * sizeof(uint16_t)));
        payload_lens = static_cast<uint8_t *>(malloc(signal_count));
        if (!payload_pool || !payload_lens) {
            free(payload_pool);
            free(payload_lens);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "export memory allocation failed");
        }

        for (size_t i = 0; i < signal_count; ++i) {
            uint16_t *payload = payload_pool + (i * kPayloadCap);
            uint8_t payload_len = 0;
            esp_err_t err = ir_engine_get_signal_payload(signals[i].signal_id, payload, kPayloadCap, &payload_len);
            if (err != ESP_OK || payload_len != signals[i].payload_len) {
                free(payload_pool);
                free(payload_lens);
                httpd_resp_set_status(req, "500 Internal Server Error");
                char body[192];
                snprintf(body, sizeof(body),
                         "{\"status\":\"error\",\"code\":\"EXPORT_PAYLOAD_INTEGRITY_FAILED\",\"signal_id\":%lu}",
                         static_cast<unsigned long>(signals[i].signal_id));
                return send_json(req, body);
            }
            payload_lens[i] = payload_len;
        }
    }

    size_t binding_count = 0;
    const bridge_slot_state_t *bindings = nullptr;
    if (include_bindings) {
        bindings = bridge_action_get_slots(&binding_count);
    }

    uint64_t exported_at_unix = static_cast<uint64_t>(esp_timer_get_time() / 1000000ULL);
    const char *hostname = app_local_discovery_hostname();

    httpd_resp_set_type(req, "application/json");
    esp_err_t send_err = httpd_resp_sendstr_chunk(req, "{");
    if (send_err != ESP_OK) {
        goto cleanup;
    }

    char head[256];
    snprintf(head, sizeof(head),
             "\"schema_version\":1,\"board_id\":\"%s\",\"exported_at_unix\":%llu,\"scope\":\"%s\",",
             hostname ? hostname : "", static_cast<unsigned long long>(exported_at_unix), scope);
    send_err = httpd_resp_sendstr_chunk(req, head);
    if (send_err != ESP_OK) {
        goto cleanup;
    }

    char counts[128];
    snprintf(counts, sizeof(counts), "\"counts\":{\"signals\":%u,\"bindings\":%u,\"devices\":0},",
             static_cast<unsigned>(include_signals ? signal_count : 0), static_cast<unsigned>(include_bindings ? binding_count : 0));
    send_err = httpd_resp_sendstr_chunk(req, counts);
    if (send_err != ESP_OK) {
        goto cleanup;
    }

    send_err = httpd_resp_sendstr_chunk(req, "\"signals\":[");
    if (send_err != ESP_OK) {
        goto cleanup;
    }
    if (include_signals) {
        for (size_t i = 0; i < signal_count; ++i) {
            char item[320];
            int n = snprintf(item, sizeof(item),
                             "%s{\"signal_id\":%lu,\"name\":\"%s\",\"device_type\":\"%s\",\"carrier_hz\":%lu,\"repeat\":%u,\"payload_len\":%u,\"payload_ticks\":[",
                             (i == 0) ? "" : ",", static_cast<unsigned long>(signals[i].signal_id), signals[i].name,
                             signals[i].device_type, static_cast<unsigned long>(signals[i].carrier_hz), signals[i].repeat,
                             payload_lens[i]);
            if (n <= 0 || n >= static_cast<int>(sizeof(item))) {
                send_err = ESP_ERR_INVALID_SIZE;
                goto cleanup;
            }
            send_err = httpd_resp_send_chunk(req, item, n);
            if (send_err != ESP_OK) {
                goto cleanup;
            }

            uint16_t *payload = payload_pool + (i * kPayloadCap);
            for (uint8_t p = 0; p < payload_lens[i]; ++p) {
                char tick[24];
                int tick_n = snprintf(tick, sizeof(tick), "%s%u", (p == 0) ? "" : ",", payload[p]);
                if (tick_n <= 0 || tick_n >= static_cast<int>(sizeof(tick))) {
                    send_err = ESP_ERR_INVALID_SIZE;
                    goto cleanup;
                }
                send_err = httpd_resp_send_chunk(req, tick, tick_n);
                if (send_err != ESP_OK) {
                    goto cleanup;
                }
            }
            send_err = httpd_resp_sendstr_chunk(req, "]}");
            if (send_err != ESP_OK) {
                goto cleanup;
            }
        }
    }
    send_err = httpd_resp_sendstr_chunk(req, "],");
    if (send_err != ESP_OK) {
        goto cleanup;
    }

    send_err = httpd_resp_sendstr_chunk(req, "\"bindings\":[");
    if (send_err != ESP_OK) {
        goto cleanup;
    }
    if (include_bindings) {
        for (size_t i = 0; i < binding_count; ++i) {
            char item[256];
            int n = snprintf(item, sizeof(item),
                             "%s{\"slot_id\":%u,\"endpoint_id\":%u,\"role\":\"%s\",\"on_signal_id\":%lu,\"off_signal_id\":%lu,\"level_up_signal_id\":%lu,\"level_down_signal_id\":%lu}",
                             (i == 0) ? "" : ",", bindings[i].slot_id, bindings[i].endpoint_id,
                             bridge_action_get_slot_role(bindings[i].slot_id), static_cast<unsigned long>(bindings[i].on_signal_id),
                             static_cast<unsigned long>(bindings[i].off_signal_id),
                             static_cast<unsigned long>(bindings[i].level_up_signal_id),
                             static_cast<unsigned long>(bindings[i].level_down_signal_id));
            if (n <= 0 || n >= static_cast<int>(sizeof(item))) {
                send_err = ESP_ERR_INVALID_SIZE;
                goto cleanup;
            }
            send_err = httpd_resp_send_chunk(req, item, n);
            if (send_err != ESP_OK) {
                goto cleanup;
            }
        }
    }
    send_err = httpd_resp_sendstr_chunk(req, "],");
    if (send_err != ESP_OK) {
        goto cleanup;
    }

    send_err = httpd_resp_sendstr_chunk(req, include_devices ? "\"devices\":[]}" : "\"devices\":[]}");
    if (send_err != ESP_OK) {
        goto cleanup;
    }

    send_err = httpd_resp_send_chunk(req, nullptr, 0);

cleanup:
    free(payload_pool);
    free(payload_lens);
    return send_err;
}

static esp_err_t commissioning_open_post_handler(httpd_req_t *req)
{
    uint32_t timeout_s = 300;
    if (req->content_len > 0 && req->content_len < 128) {
        char body[128];
        int read_len = httpd_req_recv(req, body, req->content_len);
        if (read_len > 0) {
            body[read_len] = '\0';
            uint32_t parsed_timeout = 0;
            if (parse_u32_field(body, "timeout_s", &parsed_timeout) && parsed_timeout > 0) {
                timeout_s = parsed_timeout;
            }
        }
    }
    esp_err_t err = app_open_commissioning_window(static_cast<uint16_t>(timeout_s));
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "failed to open commissioning window");
    }
    char response[128];
    snprintf(response, sizeof(response), "{\"status\":\"ok\",\"commissioning_window\":\"opened\",\"timeout_s\":%lu}",
             static_cast<unsigned long>(timeout_s));
    return send_json(req, response);
}

static esp_err_t slot_bind_post_handler(httpd_req_t *req)
{
    uint8_t slot_id = 0;
    if (!parse_slot_id_from_uri(req->uri, &slot_id)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid slot URI");
    }

    if (req->content_len <= 0 || req->content_len > 255) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body length");
    }

    char body[256];
    int read_len = httpd_req_recv(req, body, req->content_len);
    if (read_len <= 0) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "failed to read request body");
    }
    body[read_len] = '\0';

    uint32_t on_signal_id = 0;
    uint32_t off_signal_id = 0;
    uint32_t level_up_signal_id = 0;
    uint32_t level_down_signal_id = 0;
    if (!parse_u32_field(body, "on_signal_id", &on_signal_id) ||
        !parse_u32_field(body, "off_signal_id", &off_signal_id) ||
        !parse_u32_field(body, "level_up_signal_id", &level_up_signal_id) ||
        !parse_u32_field(body, "level_down_signal_id", &level_down_signal_id)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing signal id fields");
    }

    esp_err_t err = bridge_action_bind_slot(slot_id, on_signal_id, off_signal_id, level_up_signal_id, level_down_signal_id);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "failed to save slot binding");
    }

    char response[220];
    snprintf(response, sizeof(response),
             "{\"status\":\"ok\",\"slot_id\":%u,\"on_signal_id\":%lu,\"off_signal_id\":%lu,\"level_up_signal_id\":%lu,\"level_down_signal_id\":%lu}",
             slot_id, static_cast<unsigned long>(on_signal_id), static_cast<unsigned long>(off_signal_id),
             static_cast<unsigned long>(level_up_signal_id), static_cast<unsigned long>(level_down_signal_id));
    return send_json(req, response);
}

static esp_err_t devices_get_handler(httpd_req_t *req)
{
    size_t count = 0;
    const bridge_device_t *devices = bridge_action_get_devices(&count);

    esp_err_t err = begin_json_stream(req);
    if (err != ESP_OK) {
        return err;
    }
    err = httpd_resp_sendstr_chunk(req, "{\"devices\":[");
    if (err != ESP_OK) {
        return err;
    }

    char item[320];
    for (size_t i = 0; i < count; ++i) {
        snprintf(item, sizeof(item),
                 "%s{\"device_id\":%lu,\"name\":\"%s\",\"device_type\":\"%s\",\"on_signal_id\":%lu,\"off_signal_id\":%lu,\"level_up_signal_id\":%lu,\"level_down_signal_id\":%lu}",
                 (i == 0) ? "" : ",", static_cast<unsigned long>(devices[i].device_id), devices[i].name,
                 devices[i].device_type, static_cast<unsigned long>(devices[i].on_signal_id),
                 static_cast<unsigned long>(devices[i].off_signal_id), static_cast<unsigned long>(devices[i].level_up_signal_id),
                 static_cast<unsigned long>(devices[i].level_down_signal_id));
        err = httpd_resp_sendstr_chunk(req, item);
        if (err != ESP_OK) {
            return err;
        }
    }

    err = httpd_resp_sendstr_chunk(req, "]}");
    if (err != ESP_OK) {
        return err;
    }
    return end_json_stream(req);
}

static esp_err_t device_register_post_handler(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > 255) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body length");
    }
    char body[256];
    int read_len = httpd_req_recv(req, body, req->content_len);
    if (read_len <= 0) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "failed to read request body");
    }
    body[read_len] = '\0';

    char name[40] = { 0 };
    char device_type[16] = { 0 };
    if (!parse_string_field(body, "name", name, sizeof(name))) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing name");
    }
    parse_string_field(body, "device_type", device_type, sizeof(device_type));
    if (device_type[0] == '\0') {
        strlcpy(device_type, "light", sizeof(device_type));
    }

    uint32_t device_id = 0;
    esp_err_t err = bridge_action_register_device(name, device_type, &device_id);
    if (err == ESP_ERR_NO_MEM) {
        httpd_resp_set_status(req, "409 Conflict");
        return send_json(req, "{\"status\":\"error\",\"message\":\"device capacity reached\"}");
    }
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "failed to register device");
    }

    char response[192];
    snprintf(response, sizeof(response),
             "{\"status\":\"ok\",\"device_id\":%lu,\"reboot_required\":false}", static_cast<unsigned long>(device_id));
    return send_json(req, response);
}

static esp_err_t endpoint_assign_post_handler(httpd_req_t *req)
{
    uint8_t slot_id = 0;
    if (!parse_endpoint_slot_from_uri(req->uri, &slot_id)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid endpoint URI");
    }

    if (req->content_len <= 0 || req->content_len > 128) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body length");
    }
    char body[128];
    int read_len = httpd_req_recv(req, body, req->content_len);
    if (read_len <= 0) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "failed to read request body");
    }
    body[read_len] = '\0';

    uint32_t device_id = 0;
    if (!parse_u32_field(body, "device_id", &device_id)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing device_id");
    }

    esp_err_t err = bridge_action_assign_slot(slot_id, device_id);
    if (err == ESP_ERR_NOT_FOUND) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "device not found");
    }
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "failed to assign endpoint");
    }

    char response[160];
    snprintf(response, sizeof(response), "{\"status\":\"ok\",\"slot_id\":%u,\"device_id\":%lu}", slot_id,
             static_cast<unsigned long>(device_id));
    return send_json(req, response);
}

static esp_err_t device_rename_post_handler(httpd_req_t *req)
{
    uint32_t device_id = 0;
    if (!parse_device_id_from_uri(req->uri, &device_id)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid device URI");
    }

    if (req->content_len <= 0 || req->content_len > 255) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body length");
    }

    char body[256];
    int read_len = httpd_req_recv(req, body, req->content_len);
    if (read_len <= 0) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "failed to read request body");
    }
    body[read_len] = '\0';

    char name[40] = { 0 };
    if (!parse_string_field(body, "name", name, sizeof(name)) || name[0] == '\0') {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing name");
    }

    esp_err_t err = bridge_action_rename_device(device_id, name);
    if (err == ESP_ERR_NOT_FOUND) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "device not found");
    }
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "failed to rename device");
    }

    char response[192];
    snprintf(response, sizeof(response), "{\"status\":\"ok\",\"device_id\":%lu,\"name\":\"%s\"}",
             static_cast<unsigned long>(device_id), name);
    return send_json(req, response);
}

static esp_err_t learn_start_post_handler(httpd_req_t *req)
{
    uint32_t timeout_ms = 15000;
    if (req->content_len > 0 && req->content_len < 128) {
        char body[128];
        int read_len = httpd_req_recv(req, body, req->content_len);
        if (read_len > 0) {
            body[read_len] = '\0';
            uint32_t parsed_timeout = 0;
            if (parse_u32_field(body, "timeout_s", &parsed_timeout) && parsed_timeout > 0) {
                timeout_ms = parsed_timeout * 1000;
            } else if (parse_u32_field(body, "timeout_ms", &parsed_timeout) && parsed_timeout > 0) {
                timeout_ms = parsed_timeout;
            }
        }
    }

    esp_err_t err = ir_engine_start_learning(timeout_ms);
    if (err == ESP_ERR_INVALID_STATE) {
        httpd_resp_set_status(req, "409 Conflict");
        return send_json(req, "{\"status\":\"error\",\"message\":\"learning already in progress\"}");
    }
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "failed to start learning");
    }

    return send_json(req, "{\"status\":\"ok\",\"learning\":\"started\"}");
}

static esp_err_t learn_commit_post_handler(httpd_req_t *req)
{
    char name[48] = { 0 };
    char device_type[24] = { 0 };

    if (req->content_len > 0 && req->content_len < 256) {
        char body[256];
        int read_len = httpd_req_recv(req, body, req->content_len);
        if (read_len > 0) {
            body[read_len] = '\0';
            parse_string_field(body, "name", name, sizeof(name));
            parse_string_field(body, "device_type", device_type, sizeof(device_type));
        }
    }

    uint32_t signal_id = 0;
    esp_err_t err = ir_engine_commit_learning(name, device_type, &signal_id);
    if (err == ESP_ERR_INVALID_STATE) {
        httpd_resp_set_status(req, "409 Conflict");
        return send_json(req, "{\"status\":\"error\",\"message\":\"no pending learned signal\"}");
    }
    if (err == ESP_ERR_NO_MEM) {
        httpd_resp_set_status(req, "507 Insufficient Storage");
        return send_json(req, "{\"status\":\"error\",\"message\":\"signal storage full\"}");
    }
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "failed to commit learned signal");
    }

    char response[192];
    snprintf(response, sizeof(response), "{\"status\":\"ok\",\"signal_id\":%lu}", static_cast<unsigned long>(signal_id));
    return send_json(req, response);
}

static esp_err_t learn_status_get_handler(httpd_req_t *req)
{
    ir_learning_status_t status;
    ir_engine_get_learning_status(&status);

    const char *state_str = "unknown";
    switch (status.state) {
    case IR_LEARNING_IDLE:
        state_str = "idle";
        break;
    case IR_LEARNING_IN_PROGRESS:
        state_str = "in_progress";
        break;
    case IR_LEARNING_READY:
        state_str = "ready";
        break;
    case IR_LEARNING_FAILED:
        state_str = "failed";
        break;
    default:
        break;
    }

    char response[256];
    snprintf(response, sizeof(response),
             "{\"state\":\"%s\",\"elapsed_ms\":%lu,\"timeout_ms\":%lu,\"last_signal_id\":%lu,\"rx_source\":%u,\"captured_len\":%u,\"quality_score\":%u}",
             state_str, static_cast<unsigned long>(status.elapsed_ms), static_cast<unsigned long>(status.timeout_ms),
             static_cast<unsigned long>(status.last_signal_id), status.rx_source, status.captured_len,
             status.quality_score);
    return send_json(req, response);
}

static esp_err_t signals_get_handler(httpd_req_t *req)
{
    const ir_signal_record_t *signals = nullptr;
    size_t count = 0;
    esp_err_t err = ir_engine_get_signals(&signals, &count);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "failed to read signals");
    }

    httpd_resp_set_type(req, "application/json");
    esp_err_t send_err = httpd_resp_sendstr_chunk(req, "{\"signals\":[");
    if (send_err != ESP_OK) {
        return send_err;
    }

    for (size_t i = 0; i < count; ++i) {
        const ir_signal_record_t &sig = signals[i];
        char item[256];
        int n = snprintf(item, sizeof(item),
                         "%s{\"signal_id\":%lu,\"name\":\"%s\",\"device_type\":\"%s\",\"carrier_hz\":%lu,\"repeat\":%u,\"payload_len\":%u}",
                         (i == 0) ? "" : ",", static_cast<unsigned long>(sig.signal_id), sig.name, sig.device_type,
                         static_cast<unsigned long>(sig.carrier_hz), sig.repeat, sig.payload_len);
        if (n <= 0 || n >= static_cast<int>(sizeof(item))) {
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "signals item too large");
        }
        send_err = httpd_resp_send_chunk(req, item, n);
        if (send_err != ESP_OK) {
            return send_err;
        }
    }

    send_err = httpd_resp_sendstr_chunk(req, "]}");
    if (send_err != ESP_OK) {
        return send_err;
    }
    return httpd_resp_send_chunk(req, nullptr, 0);
}

static esp_err_t signal_delete_handler(httpd_req_t *req)
{
    uint32_t signal_id = 0;
    if (!parse_signal_id_from_uri(req->uri, &signal_id)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid signal URI");
    }

    esp_err_t unbind_err = bridge_action_unbind_signal_references(signal_id);
    if (unbind_err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "failed to clear signal bindings");
    }

    esp_err_t err = ir_engine_delete_signal(signal_id);
    if (err == ESP_ERR_NOT_FOUND) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "signal not found");
    }
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "failed to delete signal");
    }

    char response[128];
    snprintf(response, sizeof(response), "{\"status\":\"ok\",\"signal_id\":%lu}", static_cast<unsigned long>(signal_id));
    return send_json(req, response);
}

esp_err_t app_web_server_start()
{
    if (s_server) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 20;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %d", err);
        return err;
    }

    const httpd_uri_t health_uri = {
        .uri = "/api/health",
        .method = HTTP_GET,
        .handler = health_get_handler,
        .user_ctx = nullptr,
    };
    err = register_uri_handler_checked(s_server, &health_uri);
    if (err != ESP_OK) {
        return err;
    }

    const httpd_uri_t slots_uri = {
        .uri = "/api/slots",
        .method = HTTP_GET,
        .handler = slots_get_handler,
        .user_ctx = nullptr,
    };
    err = register_uri_handler_checked(s_server, &slots_uri);
    if (err != ESP_OK) {
        return err;
    }

    const httpd_uri_t slots_bind_uri = {
        .uri = "/api/slots/?*",
        .method = HTTP_POST,
        .handler = slot_bind_post_handler,
        .user_ctx = nullptr,
    };
    err = register_uri_handler_checked(s_server, &slots_bind_uri);
    if (err != ESP_OK) {
        return err;
    }

    const httpd_uri_t learn_start_uri = {
        .uri = "/api/learn/start",
        .method = HTTP_POST,
        .handler = learn_start_post_handler,
        .user_ctx = nullptr,
    };
    err = register_uri_handler_checked(s_server, &learn_start_uri);
    if (err != ESP_OK) {
        return err;
    }

    const httpd_uri_t learn_status_uri = {
        .uri = "/api/learn/status",
        .method = HTTP_GET,
        .handler = learn_status_get_handler,
        .user_ctx = nullptr,
    };
    err = register_uri_handler_checked(s_server, &learn_status_uri);
    if (err != ESP_OK) {
        return err;
    }

    const httpd_uri_t learn_commit_uri = {
        .uri = "/api/learn/commit",
        .method = HTTP_POST,
        .handler = learn_commit_post_handler,
        .user_ctx = nullptr,
    };
    err = register_uri_handler_checked(s_server, &learn_commit_uri);
    if (err != ESP_OK) {
        return err;
    }

    const httpd_uri_t signals_uri = {
        .uri = "/api/signals",
        .method = HTTP_GET,
        .handler = signals_get_handler,
        .user_ctx = nullptr,
    };
    err = register_uri_handler_checked(s_server, &signals_uri);
    if (err != ESP_OK) {
        return err;
    }

    const httpd_uri_t signal_delete_uri = {
        .uri = "/api/signals/*",
        .method = HTTP_DELETE,
        .handler = signal_delete_handler,
        .user_ctx = nullptr,
    };
    err = register_uri_handler_checked(s_server, &signal_delete_uri);
    if (err != ESP_OK) {
        return err;
    }

    const httpd_uri_t devices_uri = {
        .uri = "/api/devices",
        .method = HTTP_GET,
        .handler = devices_get_handler,
        .user_ctx = nullptr,
    };
    err = register_uri_handler_checked(s_server, &devices_uri);
    if (err != ESP_OK) {
        return err;
    }

    const httpd_uri_t device_register_uri = {
        .uri = "/api/devices/register",
        .method = HTTP_POST,
        .handler = device_register_post_handler,
        .user_ctx = nullptr,
    };
    err = register_uri_handler_checked(s_server, &device_register_uri);
    if (err != ESP_OK) {
        return err;
    }

    const httpd_uri_t device_rename_uri = {
        .uri = "/api/devices/*",
        .method = HTTP_POST,
        .handler = device_rename_post_handler,
        .user_ctx = nullptr,
    };
    err = register_uri_handler_checked(s_server, &device_rename_uri);
    if (err != ESP_OK) {
        return err;
    }

    const httpd_uri_t endpoint_assign_uri = {
        .uri = "/api/endpoints/*",
        .method = HTTP_POST,
        .handler = endpoint_assign_post_handler,
        .user_ctx = nullptr,
    };
    err = register_uri_handler_checked(s_server, &endpoint_assign_uri);
    if (err != ESP_OK) {
        return err;
    }

    const httpd_uri_t export_nvs_uri = {
        .uri = "/api/export/nvs",
        .method = HTTP_GET,
        .handler = export_nvs_get_handler,
        .user_ctx = nullptr,
    };
    err = register_uri_handler_checked(s_server, &export_nvs_uri);
    if (err != ESP_OK) {
        return err;
    }

    const httpd_uri_t commissioning_open_uri = {
        .uri = "/api/commissioning/open",
        .method = HTTP_POST,
        .handler = commissioning_open_post_handler,
        .user_ctx = nullptr,
    };
    err = register_uri_handler_checked(s_server, &commissioning_open_uri);
    if (err != ESP_OK) {
        return err;
    }

    const httpd_uri_t dashboard_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = dashboard_get_handler,
        .user_ctx = nullptr,
    };
    err = register_uri_handler_checked(s_server, &dashboard_uri);
    if (err != ESP_OK) {
        return err;
    }

    const httpd_uri_t favicon_uri = {
        .uri = "/favicon.ico",
        .method = HTTP_GET,
        .handler = no_content_get_handler,
        .user_ctx = nullptr,
    };
    err = register_uri_handler_checked(s_server, &favicon_uri);
    if (err != ESP_OK) {
        return err;
    }

    const httpd_uri_t apple_icon_uri = {
        .uri = "/apple-touch-icon.png",
        .method = HTTP_GET,
        .handler = no_content_get_handler,
        .user_ctx = nullptr,
    };
    err = register_uri_handler_checked(s_server, &apple_icon_uri);
    if (err != ESP_OK) {
        return err;
    }

    const httpd_uri_t apple_icon_precomposed_uri = {
        .uri = "/apple-touch-icon-precomposed.png",
        .method = HTTP_GET,
        .handler = no_content_get_handler,
        .user_ctx = nullptr,
    };
    err = register_uri_handler_checked(s_server, &apple_icon_precomposed_uri);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG,
             "HTTP API started: GET /, GET /api/health, GET /api/slots, GET /api/signals, DELETE /api/signals/{id}, GET /api/devices, GET /api/export/nvs, POST /api/devices/register, POST /api/devices/{id}/rename, POST /api/endpoints/{slot}/assign, POST /api/slots/{id}/bind, POST /api/learn/start, GET /api/learn/status, POST /api/learn/commit, POST /api/commissioning/open");
    return ESP_OK;
}
