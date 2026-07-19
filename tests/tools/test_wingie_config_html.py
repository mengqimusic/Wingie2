from html.parser import HTMLParser
from pathlib import Path
import re
import shutil
import subprocess
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
HTML_PATH = REPO_ROOT / "Tools/wingie_config.html"
MOCK_PATH = REPO_ROOT / "tests/tools/wingie_serial_mock.js"


class DocumentParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids = []
        self.external_assets = []
        self.script_count = 0
        self.style_count = 0

    def handle_starttag(self, tag, attributes):
        values = dict(attributes)
        if "id" in values:
            self.ids.append(values["id"])
        if tag == "script":
            self.script_count += 1
            if values.get("src"):
                self.external_assets.append(values["src"])
        if tag == "style":
            self.style_count += 1
        if tag == "link" and values.get("href"):
            self.external_assets.append(values["href"])


class WingieConfigHtmlTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = HTML_PATH.read_text(encoding="utf-8")
        cls.mock_source = MOCK_PATH.read_text(encoding="utf-8")
        cls.firmware_source = (REPO_ROOT / "Wingie2/serial_config.ino").read_text(encoding="utf-8")
        cls.protocol_source = (REPO_ROOT / "Wingie2/serial_config_protocol.h").read_text(encoding="utf-8")
        cls.control_source = (REPO_ROOT / "Wingie2/control.ino").read_text(encoding="utf-8")
        cls.midi_source = (REPO_ROOT / "Wingie2/MIDI.ino").read_text(encoding="utf-8")
        cls.main_source = (REPO_ROOT / "Wingie2/Wingie2.ino").read_text(encoding="utf-8")
        cls.storage_source = (REPO_ROOT / "Wingie2/stuff.ino").read_text(encoding="utf-8")
        cls.parser = DocumentParser()
        cls.parser.feed(cls.source)

    def test_is_one_self_contained_document(self):
        self.assertEqual(self.parser.script_count, 1)
        self.assertEqual(self.parser.style_count, 1)
        self.assertEqual(self.parser.external_assets, [])
        self.assertEqual(len(self.parser.ids), len(set(self.parser.ids)))
        self.assertNotRegex(self.source, r"https?://")
        self.assertNotIn("fetch(", self.source)
        self.assertNotIn("WebSocket", self.source)

    def test_uses_schema_four_snapshot_protocol(self):
        self.assertIn("Number(hello.config_schema) !== 4", self.source)
        for operation in (
            "hello",
            "get_settings",
            "get",
            "get_cave",
            "set_param",
            "set",
            "set_cave",
            "reset",
            "save",
        ):
            self.assertRegex(self.source, rf'"{operation}"')
        self.assertIn("navigator.serial.requestPort()", self.source)
        self.assertIn("expected_revision", self.source)
        self.assertIn("setInterval", self.source)
        self.assertNotIn("heartbeat", self.source.lower())
        self.assertNotIn('"changed"', self.source)
        self.assertNotIn("set_ratio_value", self.source)
        self.assertNotIn("set_cave_value", self.source)
        self.assertNotIn("set_cave_mute", self.source)
        self.assertIn('code !== "busy"', self.source)
        self.assertIn("await helloWhenReady();", self.source)

    def test_connect_refresh_and_poll_use_complete_snapshots(self):
        snapshot_reader = re.search(
            r"async function readFullSnapshot\(\) \{(.*?)\n      \}",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(snapshot_reader)
        snapshot_block = snapshot_reader.group(1)
        self.assertEqual(snapshot_block.count('request("get_settings")'), 1)
        self.assertEqual(snapshot_block.count('request("get")'), 1)
        self.assertEqual(snapshot_block.count("await readCaveResponses()"), 1)

        refresh = re.search(
            r"async function refreshAll\(announce = true\) \{(.*?)\n      \}",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(refresh)
        block = refresh.group(1)
        self.assertIn("applyFullSnapshot(await readFullSnapshot(), false)", block)

        poll = re.search(
            r"async function pollFullSnapshot\(\) \{(.*?)\n      \}",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(poll)
        self.assertIn("const snapshot = await readFullSnapshot()", poll.group(1))
        self.assertIn("applyFullSnapshot(snapshot, true)", poll.group(1))
        self.assertIn("window.setInterval", self.source)
        self.assertIn("}, 1000);", self.source)
        cave_reader = re.search(
            r"async function readCaveResponses\(\) \{(.*?)\n      \}",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(cave_reader)
        self.assertEqual(cave_reader.group(1).count('request("get_cave"'), 1)
        self.assertIn("for (const side of sides)", cave_reader.group(1))
        self.assertIn("for (let bank = 0; bank < bankCount; bank += 1)", cave_reader.group(1))
        self.assertIn("#wg-refresh", self.source)

    def test_poll_status_and_header_controls_are_bilingual(self):
        self.assertIn('id="wg-poll-status"', self.source)
        self.assertIn('class="wg-connect-controls"', self.source)
        self.assertIn("连接后每秒同步设备状态", self.source)
        self.assertIn("Connect to sync device state every second", self.source)
        self.assertIn("正在同步设备状态…", self.source)
        self.assertIn("Syncing device state…", self.source)
        self.assertIn("每秒同步设备状态；编辑或写入期间自动跳过", self.source)
        self.assertIn("Syncing device state every second; pauses while editing or writing", self.source)
        self.assertIn("每秒从设备读取一次完整配置", self.source)
        self.assertIn("Reading the full device configuration every second", self.source)
        self.assertIn("实时写入期间暂停轮询", self.source)
        self.assertIn("Polling pauses during live writes", self.source)
        self.assertIn("justify-items: end", self.source)
        self.assertIn("text-align: right", self.source)
        self.assertIn("updatePollingStatus()", self.source)

    def test_light_poll_uses_status_with_legacy_fallback(self):
        self.assertIn("lightPolling: true,", self.source)
        tick = re.search(
            r"function pollTick\(\) \{(.*?)\n      \}",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(tick)
        self.assertIn("state.lightPolling === false ? pollFullSnapshot() : pollLightSnapshot();", tick.group(1))
        starter = re.search(
            r"function startPolling\(\) \{(.*?)\n      \}",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(starter)
        self.assertIn("pollTick().catch(() => undefined);", starter.group(1))
        light = re.search(
            r"async function pollLightSnapshot\(\) \{(.*?)\n      \}",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(light)
        block = light.group(1)
        self.assertIn("if (!state.connected || state.polling || state.refreshing || state.saving || pendingUiWriteCount()) return;", block)
        self.assertEqual(block.count('request("status")'), 1)
        self.assertEqual(block.count('request("get_settings")'), 1)
        self.assertIn("statusResponse.cave_revision", block)
        self.assertIn("state.lightPolling = false;", block)
        self.assertIn("if (fallback) await pollFullSnapshot();", block)
        self.assertIn('Number(statusResponse.profile_revision) !== state.ratio.revision', block)
        self.assertIn('ratioSnapshot = normalizeRatio(await request("get"));', block)
        self.assertIn('Number(statusResponse.cave_active_bank[side])', block)
        self.assertIn('normalizeCave(await request("get_cave", {side, bank}))', block)
        self.assertIn("if (!state.connected || epoch !== state.connectionEpoch || pendingUiWriteCount()) return;", block)
        self.assertIn("applyRatioSnapshot(ratioSnapshot, true);", block)
        self.assertIn("applyCaveBankSnapshot(item.side, item.bank, item.normalized, true);", block)
        self.assertIn("if (!parameterEditIsPending(edit)) state.edits.delete(key);", block)
        disconnect = re.search(
            r"async function disconnect\(announce = true\) \{(.*?)\n      \}",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(disconnect)
        self.assertIn("state.lightPolling = true;", disconnect.group(1))
        self.assertIn("pollTick,", self.source)
        self.assertIn("setLegacyFirmware(value)", self.mock_source)
        self.assertIn("cave_revision", self.mock_source)
        self.assertIn("legacyFirmware", self.mock_source)

    def test_has_complete_controls_and_omits_runtime_status(self):
        for target in ("left", "right"):
            for name in ("mode", "threshold"):
                self.assertIn(f'param:{target}:{name}', self.source)
        for name in (
            "a3_hz",
            "tuning",
            "pre_clip_gain",
            "post_clip_gain",
            "midi_left",
            "midi_right",
            "midi_both",
        ):
            self.assertIn(f'param:shared:{name}', self.source)
        self.assertNotIn("param:shared:mpe_enabled", self.source)
        self.assertNotIn("wg-mpe-enabled", self.source)
        self.assertIn("config_schema) !== 4", self.source)
        for stale_item in ("Source", "Note", "Fundamental", "Active Cave", "mix", "decay", "volume"):
            self.assertNotIn(stale_item, self.source)
        for runtime_key in (
            "param:left:mix", "param:left:decay", "param:left:volume",
            "param:right:mix", "param:right:decay", "param:right:volume",
        ):
            self.assertNotIn(runtime_key, self.source)
        self.assertIn("Factory Ratio", self.source)
        self.assertIn("Export JSON", self.source)
        self.assertIn("Import JSON", self.source)
        self.assertNotIn(">Apply", self.source)

    def test_user_facing_copy_uses_explicit_language_fields(self):
        for phrase in (
            "id=\"wg-language\"",
            "state.language === \"zh\" ? \"en\" : \"zh\"",
            "applyLanguage()",
            "data-i18n-zh=\"连接 Wingie2\"",
            "data-i18n-en=\"Connect Wingie2\"",
            "data-i18n-zh=\"自定义比例模式设定\"",
            "data-i18n-en=\"Ratio Mode Profile\"",
            "data-i18n-zh=\"保存到闪存\"",
            "data-i18n-en=\"Save to Flash\"",
        ):
            self.assertIn(phrase, self.source)
        self.assertNotIn("chooseLanguage", self.source)
        self.assertNotIn("bilingualOriginal", self.source)
        self.assertNotIn("硬件左侧", self.source)
        self.assertNotIn("Hardware left", self.source)

    def test_ratio_slot_groups_and_copy_slot_button(self):
        self.assertIn("const ratioSlotSize = 3;", self.source)
        self.assertIn("#wingie-config .wg-ratio-slot td", self.source)
        self.assertIn('slotRow.className = "wg-ratio-slot";', self.source)
        self.assertIn("index % ratioSlotSize === 0", self.source)
        self.assertIn('data-i18n-zh="槽 ${slot}"', self.source)
        self.assertIn('data-i18n-en="Slot ${slot}"', self.source)
        for phrase in (
            'data-i18n-zh="Ratio 复音模式下，声部 1/2/3 分别使用槽 1/2/3 的比例。"',
            'data-i18n-en="In Ratio poly mode, voices 1/2/3 use the ratios of Slots 1/2/3 respectively."',
            'id="wg-copy-slot1"',
            'class="wg-button"',
            'data-i18n-zh="复制槽 1 到槽 2、3"',
            'data-i18n-en="Copy Slot 1 to Slots 2&amp;3"',
        ):
            self.assertIn(phrase, self.source)
        ratio_section = self.source.split('id="wg-ratio-title"', 1)[1].split("</section>", 1)[0]
        self.assertLess(ratio_section.index('id="wg-copy-slot1"'), ratio_section.index('id="wg-factory"'))
        copy_slot = re.search(r"function copySlot1ToSlots\(\) \{(.*?)\n      \}", self.source, re.DOTALL)
        self.assertIsNotNone(copy_slot)
        block = copy_slot.group(1)
        self.assertIn("state.ratio.draft[index] = state.ratio.draft[index % ratioSlotSize]", block)
        self.assertIn("state.ratio.raw[index] = formatNumber(state.ratio.draft[index], state.ratio.limits.step)", block)
        self.assertIn('scheduleResource("ratio", commitRatio, false)', block)
        self.assertNotIn("request(", block)
        self.assertIn('copySlot1: root.querySelector("#wg-copy-slot1")', self.source)
        self.assertIn("elements.copySlot1.disabled = !ready || busy || state.pendingWrites > 0;", self.source)
        self.assertIn('elements.copySlot1.addEventListener("click", copySlot1ToSlots);', self.source)

    def test_minimal_responsive_visual_contract(self):
        self.assertIn("background: #fff", self.source)
        self.assertIn("width: min(1120px, 100%)", self.source)
        self.assertIn("grid-template-columns: repeat(2, minmax(0, 1fr))", self.source)
        self.assertIn("grid-template-columns: repeat(6, minmax(0, 1fr))", self.source)
        self.assertEqual(self.source.count('class="wg-field wg-field-half"'), 4)
        self.assertEqual(self.source.count('class="wg-field wg-field-third"'), 3)
        self.assertEqual(self.source.count('class="wg-field wg-field-full"'), 0)
        self.assertIn("border-radius: 4px", self.source)
        self.assertIn("@media (max-width: 760px)", self.source)
        self.assertIn('data-mobile-side="left"', self.source)
        self.assertIn("overflow-x: auto", self.source)
        self.assertNotIn("box-shadow", self.source)
        self.assertNotIn("linear-gradient", self.source)
        self.assertNotIn("radial-gradient", self.source)

    def test_full_resource_debounce_and_save_flush(self):
        self.assertIn("window.setTimeout(run, 150)", self.source)
        self.assertIn('request("set", {expected_revision:', self.source)
        self.assertIn('request("set_cave", {', self.source)
        self.assertIn("await waitForWrites();", self.source)
        self.assertIn('request("save", {}, 6000)', self.source)
        self.assertIn("state.writeChain", self.source)
        self.assertIn("state.resourceVersions", self.source)
        self.assertIn("await refreshDependentCaves();", self.source)
        self.assertIn("await acknowledge(response", self.source)

    def test_write_poll_race_guards(self):
        enqueue = re.search(
            r"function enqueueWrite\(key, version, execute, acknowledge, rollback\) \{(.*?)\n      \}",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(enqueue)
        self.assertIn("if (state.pollPromise) await state.pollPromise;", enqueue.group(1))
        poll = re.search(
            r"async function pollFullSnapshot\(\) \{(.*?)\n      \}",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(poll)
        self.assertIn(
            "if (!state.connected || epoch !== state.connectionEpoch || pendingUiWriteCount()) return;",
            poll.group(1),
        )
        ratio_apply = re.search(
            r"function applyRatioSnapshot\(ratio, preservePending\) \{(.*?)\n      \}",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(ratio_apply)
        block = ratio_apply.group(1)
        self.assertIn("ratio.revision = previous.revision;", block)
        self.assertIn("ratio.values = previous.values.slice();", block)
        self.assertIn("ratio.dirty = previous.dirty;", block)
        self.assertIn("ratio.draft = previous.draft.slice();", block)
        cave_apply = re.search(
            r"function applyCaveBankSnapshot\(side, bank, normalized, preservePending\) \{(.*?)\n      \}",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(cave_apply)
        block = cave_apply.group(1)
        self.assertIn("normalized.revision = current.revision;", block)
        self.assertIn("normalized.frequencies = current.frequencies.slice();", block)
        self.assertIn("normalized.mute = current.mute.slice();", block)
        self.assertIn("normalized.dirty = current.dirty;", block)
        self.assertIn("normalized.draftFrequencies = current.draftFrequencies.slice();", block)
        self.assertIn("applyCaveBankSnapshot(side, bank, normalizeCave(responses[side][bank]), preservePending);", self.source)

    def test_revision_conflict_self_heals(self):
        enqueue = re.search(
            r"function enqueueWrite\(key, version, execute, acknowledge, rollback\) \{(.*?)\n      \}",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(enqueue)
        block = enqueue.group(1)
        self.assertIn('code === "revision_conflict"', block)
        self.assertIn("!state.conflictRetries.has(key)", block)
        self.assertIn("await resyncConflictedResource(key);", block)
        self.assertIn("state.conflictRetries.delete(key);", block)
        resync = re.search(
            r"async function resyncConflictedResource\(key\) \{(.*?)\n      \}",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(resync)
        block = resync.group(1)
        self.assertIn("state.conflictRetries.add(key);", block)
        self.assertIn('applyRatioSnapshot(normalizeRatio(await request("get")), true);', block)
        self.assertIn('scheduleResource("ratio", commitRatio, true);', block)
        self.assertIn('applyCaveBankSnapshot(side, bank, normalizeCave(await request("get_cave", {side, bank})), true);', block)
        self.assertIn("scheduleResource(key, (version) => commitCave(side, bank, version), true);", block)
        self.assertIn("conflictRetries: new Set(),", self.source)
        reset = re.search(
            r"async function resetFactoryRatio\(\) \{(.*?)\n      \}",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(reset)
        block = reset.group(1)
        self.assertIn('code !== "revision_conflict"', block)
        self.assertIn('applyRatioSnapshot(normalizeRatio(await request("get")), true);', block)
        reset_mock = re.search(
            r'if \(request\.op === "reset"\) \{(.*?)\n    \}',
            self.mock_source,
            re.DOTALL,
        )
        self.assertIsNotNone(reset_mock)
        self.assertIn('error: {code: "revision_conflict", field: "expected_revision"}', reset_mock.group(1))

    def test_mock_matches_schema_four_without_events(self):
        self.assertIn("config_schema: 4", self.mock_source)
        for operation in (
            "get_settings",
            "status",
            "set_param",
            "set",
            "set_cave",
            "save",
        ):
            self.assertIn(f'request.op === "{operation}"', self.mock_source)
        self.assertIn("setSettings(values)", self.mock_source)
        self.assertIn("max_frame: 512", self.mock_source)
        self.assertIn("caves_changed: cavesChanged", self.mock_source)
        self.assertNotIn('event: "changed"', self.mock_source)

    def test_disconnect_releases_read_loop_before_closing(self):
        self.assertIn("state.disconnecting = true;", self.source)
        self.assertIn("!state.disconnecting && state.port && state.port.readable", self.source)
        self.assertIn("await state.readLoop;", self.source)
        self.assertIn("state.disconnecting = false;", self.source)
        disconnect_block = self.source.split("async function disconnect(", 1)[1]
        self.assertLess(disconnect_block.index("await state.readLoop"), disconnect_block.index("state.port.close"))
        self.assertIn("readable.locked", self.mock_source)

    def test_firmware_uses_snapshot_settings_without_runtime_sync(self):
        self.assertIn('"config_schema\\":4', self.firmware_source)
        self.assertIn("kOperationGetSettings", self.protocol_source)
        self.assertIn("kOperationSetParam", self.protocol_source)
        self.assertIn("void sendSettings(uint32_t id)", self.firmware_source)
        self.assertIn("bool applyScalarParameter", self.firmware_source)
        self.assertIn('"caves_changed\\":%s', self.firmware_source)
        self.assertIn("generalSettingsAreDirty()", self.firmware_source)
        self.assertIn("cavesChanged = tune_caves();", self.firmware_source)
        self.assertRegex(
            self.midi_source,
            r'if \(use_alt_tuning && alt_tuning_index >= 0\) tune_caves\(\);\s*apply_note_profiles_to_dsp\(\);',
        )
        self.assertIn("bool tune_caves()", self.main_source)
        self.assertIn("> 0.0051f", self.main_source)
        self.assertIn("dsp.getParamValue", self.firmware_source)
        self.assertIn("struct CaveBankSnapshot", self.firmware_source)
        self.assertIn("const uint32_t currentRevision", self.firmware_source)
        self.assertIn("bool revisionConflict = false;", self.firmware_source)
        self.assertIn("if (!isfinite(value)) return false;", self.firmware_source)
        reset_block = self.firmware_source.split("case wingie_serial::kOperationReset:", 1)[1]
        self.assertIn("request.expectedRevision != ratio_profile.revision", reset_block)
        self.assertIn("apply_channel_mode_change(ch);", self.control_source)
        self.assertIn("void apply_channel_mode_change(byte ch)", self.main_source)
        self.assertIn("bool save_all_preferences()", self.storage_source)
        self.assertIn("void service_preferences_save()", self.storage_source)
        self.assertIn("request_preferences_save();", self.control_source)
        self.assertNotIn("save_stuff();", self.control_source)
        self.assertIn("service_preferences_save();", self.main_source)
        self.assertIn("if (!serial_config_ready) return;", self.main_source)
        self.assertIn("tuning_preferences_dirty", self.storage_source)
        self.assertNotIn("mpe_enabled", self.storage_source)
        self.assertNotIn("mpe_enabled", self.firmware_source)
        self.assertIn('"capabilities\\\":[\\\"settings\\\",\\\"ratio_mode\\\",\\\"cave_config\\\",\\\"mpe\\\"]', self.firmware_source)
        self.assertIn('"config_schema\\\":4', self.firmware_source)
        self.assertRegex(
            self.storage_source,
            r"if \(unq_caves_store\) \{\s*if \(!save_general_preferences\(prefs\)\)",
        )
        for source, index in (
            (self.firmware_source, "performanceIndex"),
            (self.midi_source, "MIX"),
        ):
            sampled = source.index(f"potValSampled[{index}] = potValRealtime[{index}];")
            released = source.index(f"realtime_value_valid[{index}] = false;")
            self.assertLess(sampled, released)
        combined = self.firmware_source + self.protocol_source + self.control_source + self.main_source
        for forbidden in ("config_runtime", "config_event_pending", "sendChangedEvent", "get_state"):
            self.assertNotIn(forbidden, combined)

    def test_inline_javascript_and_mock_parse(self):
        node = shutil.which("node")
        if not node:
            self.skipTest("node is not installed")
        match = re.search(r"<script>(.*)</script>", self.source, re.DOTALL)
        self.assertIsNotNone(match)
        for command, source in (
            ([node, "--check", "-"], match.group(1)),
            ([node, "--check", str(MOCK_PATH)], None),
        ):
            result = subprocess.run(
                command,
                input=source,
                text=True,
                capture_output=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
