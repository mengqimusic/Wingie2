import CoreMIDI
import Foundation

private let destinationName = "USB MIDI DevicePort 1"

private func endpointName(_ endpoint: MIDIEndpointRef) -> String {
    var value: Unmanaged<CFString>?
    guard MIDIObjectGetStringProperty(endpoint, kMIDIPropertyDisplayName, &value) == noErr,
          let name = value?.takeRetainedValue() else { return "" }
    return name as String
}

private func destination(named name: String) -> MIDIEndpointRef {
    for index in 0..<MIDIGetNumberOfDestinations() {
        let endpoint = MIDIGetDestination(index)
        if endpointName(endpoint) == name { return endpoint }
    }
    return 0
}

private func usage() -> Never {
    FileHandle.standardError.write(Data("usage: midi_stress.swift batch <channel> <pairs> <interval-us> | marker <channel> <pitch> | bend <channel> <value> | pb-range <channel> <semitones> | note-on <channel> <pitch> <bend> | note-off <channel> <pitch> | mpe-note <channel> <pitch> <bend> <hold-ms> | mpe-config <lower-members> <upper-members>\n".utf8))
    exit(2)
}

let arguments = Array(CommandLine.arguments.dropFirst())
if arguments == ["--help"] || arguments == ["-h"] { usage() }
guard let mode = arguments.first else { usage() }

let endpoint = destination(named: destinationName)
guard endpoint != 0 else { fatalError("CoreMIDI destination not found: \(destinationName)") }

var client = MIDIClientRef()
var output = MIDIPortRef()
precondition(MIDIClientCreate("Wingie2 MIDI stress" as CFString, nil, nil, &client) == noErr)
precondition(MIDIOutputPortCreate(client, "Wingie2 MIDI stress out" as CFString, &output) == noErr)
defer {
    MIDIPortDispose(output)
    MIDIClientDispose(client)
}

func send(_ bytes: [UInt8]) {
    var list = MIDIPacketList()
    withUnsafeMutablePointer(to: &list) { listPointer in
        let packet = MIDIPacketListInit(listPointer)
        bytes.withUnsafeBufferPointer { buffer in
            _ = MIDIPacketListAdd(listPointer, MemoryLayout<MIDIPacketList>.size, packet, 0,
                                  buffer.count, buffer.baseAddress!)
        }
        precondition(MIDISend(output, endpoint, listPointer) == noErr)
    }
}

func validatedChannel(_ argument: String) -> Int {
    guard let channel = Int(argument), (1...16).contains(channel) else { usage() }
    return channel
}

func sendControlChange(channel: Int, controller: UInt8, value: UInt8) {
    send([UInt8(0xB0 | (channel - 1)), controller, value])
}

func sendPitchBend(channel: Int, bend: Int) {
    guard (-8192...8191).contains(bend) else { usage() }
    let value = bend + 8192
    send([UInt8(0xE0 | (channel - 1)), UInt8(value & 0x7F), UInt8((value >> 7) & 0x7F)])
}

func sendMpeConfiguration(channel: Int, memberCount: Int) {
    guard (0...15).contains(memberCount) else { usage() }
    sendControlChange(channel: channel, controller: 101, value: 0)
    sendControlChange(channel: channel, controller: 100, value: 6)
    sendControlChange(channel: channel, controller: 6, value: UInt8(memberCount))
    sendControlChange(channel: channel, controller: 101, value: 127)
    sendControlChange(channel: channel, controller: 100, value: 127)
}

func sendPitchBendRange(channel: Int, semitones: Int) {
    guard (0...96).contains(semitones) else { usage() }
    sendControlChange(channel: channel, controller: 101, value: 0)
    sendControlChange(channel: channel, controller: 100, value: 0)
    sendControlChange(channel: channel, controller: 6, value: UInt8(semitones))
    sendControlChange(channel: channel, controller: 38, value: 0)
    sendControlChange(channel: channel, controller: 101, value: 127)
    sendControlChange(channel: channel, controller: 100, value: 127)
}

let started = ContinuousClock.now

switch mode {
case "batch":
    guard arguments.count == 4 else { usage() }
    let channel = validatedChannel(arguments[1])
    guard arguments.count == 4,
          let pairs = Int(arguments[2]), pairs > 0,
          let interval = useconds_t(arguments[3]) else { usage() }
    let noteOn = UInt8(0x90 | (channel - 1))
    let noteOff = UInt8(0x80 | (channel - 1))
    for index in 0..<pairs {
        let pitch = UInt8(36 + (index % 60))
        send([noteOn, pitch, 100])
        usleep(interval)
        send([noteOff, pitch, 0])
        usleep(interval)
    }
    print("sent channel=\(channel) note_on=\(pairs) note_off=\(pairs) elapsed=\(started.duration(to: .now))")
case "marker":
    guard arguments.count == 3 else { usage() }
    let channel = validatedChannel(arguments[1])
    guard arguments.count == 3, let pitchValue = Int(arguments[2]),
          (0...127).contains(pitchValue) else { usage() }
    let pitch = UInt8(pitchValue)
    let noteOn = UInt8(0x90 | (channel - 1))
    let noteOff = UInt8(0x80 | (channel - 1))
    send([noteOn, pitch, 100])
    usleep(100_000)
    send([noteOff, pitch, 0])
    print("marker channel=\(channel) pitch=\(pitch)")
case "bend":
    guard arguments.count == 3, let bend = Int(arguments[2]) else { usage() }
    let channel = validatedChannel(arguments[1])
    sendPitchBend(channel: channel, bend: bend)
    usleep(20_000)
    print("bend channel=\(channel) value=\(bend)")
case "pb-range":
    guard arguments.count == 3, let semitones = Int(arguments[2]) else { usage() }
    let channel = validatedChannel(arguments[1])
    sendPitchBendRange(channel: channel, semitones: semitones)
    usleep(20_000)
    print("pb-range channel=\(channel) semitones=\(semitones)")
case "note-on":
    guard arguments.count == 4,
          let pitchValue = Int(arguments[2]), (0...127).contains(pitchValue),
          let bend = Int(arguments[3]) else { usage() }
    let channel = validatedChannel(arguments[1])
    sendPitchBend(channel: channel, bend: bend)
    send([UInt8(0x90 | (channel - 1)), UInt8(pitchValue), 100])
    usleep(20_000)
    print("note-on channel=\(channel) pitch=\(pitchValue) bend=\(bend)")
case "note-off":
    guard arguments.count == 3,
          let pitchValue = Int(arguments[2]), (0...127).contains(pitchValue) else { usage() }
    let channel = validatedChannel(arguments[1])
    send([UInt8(0x80 | (channel - 1)), UInt8(pitchValue), 0])
    usleep(20_000)
    print("note-off channel=\(channel) pitch=\(pitchValue)")
case "mpe-note":
    guard arguments.count == 5,
          let pitchValue = Int(arguments[2]), (0...127).contains(pitchValue),
          let bend = Int(arguments[3]),
          let holdMilliseconds = useconds_t(arguments[4]), holdMilliseconds <= 60_000 else { usage() }
    let channel = validatedChannel(arguments[1])
    sendPitchBend(channel: channel, bend: bend)
    send([UInt8(0x90 | (channel - 1)), UInt8(pitchValue), 100])
    usleep(holdMilliseconds * 1_000)
    send([UInt8(0x80 | (channel - 1)), UInt8(pitchValue), 0])
    usleep(20_000)
    print("mpe-note channel=\(channel) pitch=\(pitchValue) bend=\(bend) hold_ms=\(holdMilliseconds)")
case "mpe-config":
    guard arguments.count == 3,
          let lowerMembers = Int(arguments[1]),
          let upperMembers = Int(arguments[2]),
          (0...15).contains(lowerMembers), (0...15).contains(upperMembers) else { usage() }
    sendMpeConfiguration(channel: 1, memberCount: lowerMembers)
    sendMpeConfiguration(channel: 16, memberCount: upperMembers)
    usleep(20_000)
    print("mpe-config lower_members=\(lowerMembers) upper_members=\(upperMembers)")
default:
    usage()
}
