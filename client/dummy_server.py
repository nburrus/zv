#!/usr/bin/env python3

from icecream import ic

import asyncio
import struct

async def read_messages(reader):
    while True:
        kind = await reader.readexactly(4)
        kind = struct.unpack('I', kind)[0] # uint32

        payloadSize = await reader.readexactly(8)
        payloadSize = struct.unpack('Q', payloadSize)[0] # uint64

        ic(kind, payloadSize)

        payload = await reader.readexactly(payloadSize)
        print (f"Read the entire payload! {len(payload)}")

        if (kind == 0):
            break

def send_version (writer):
    kind = struct.pack('I', 1)
    payloadSizeInBytes = struct.pack('Q', 4)
    version_payload = struct.pack('I', 1)
    writer.write (kind)
    writer.write (payloadSizeInBytes)
    writer.write (version_payload)

def send_close (writer):
    kind = struct.pack('I', 0)
    payloadSizeInBytes = struct.pack('Q', 0)
    writer.write (kind)
    writer.write (payloadSizeInBytes)

async def handle_client(reader: asyncio.StreamReader, writer):
    read_loop = asyncio.create_task(read_messages(reader))
    
    send_version (writer)
    await writer.drain()

    await read_loop
    
    print("Close the connection")
    send_close (writer)
    writer.close()

async def main():
    server = await asyncio.start_server(
        handle_client, '127.0.0.1', 4207)

    addr = server.sockets[0].getsockname()
    print(f'Serving on {addr}')

    async with server:
        await server.serve_forever()

asyncio.run(main())
