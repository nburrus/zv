use std::io::{self, Read, Write};

use serde::{Deserialize, Serialize};

pub const PROTOCOL_VERSION: u16 = 1;
pub const CAPABILITY_READ_IMAGES: u32 = 1 << 0;
pub const KNOWN_CAPABILITIES: u32 = CAPABILITY_READ_IMAGES;

const MAX_FRAME_SIZE: usize = 512 * 1024 * 1024;

#[derive(Clone, Debug, Eq, PartialEq, Deserialize, Serialize)]
pub struct ImageOffer {
    pub id: u64,
    pub name: String,
    pub remote_path: String,
    pub width: Option<u32>,
    pub height: Option<u32>,
    pub format_hint: Option<String>,
}

#[derive(Clone, Debug, Eq, PartialEq, Deserialize, Serialize)]
#[serde(tag = "type", content = "body", rename_all = "snake_case")]
pub enum Message {
    Hello {
        version: u16,
        capabilities: u32,
    },
    ImageOffer(ImageOffer),
    RequestImageData {
        id: u64,
    },
    ImageData {
        id: u64,
        #[serde(with = "serde_bytes")]
        encoded_bytes: Vec<u8>,
    },
    CommandResult {
        request_id: u64,
        succeeded: bool,
        message: String,
    },
    Error {
        id: Option<u64>,
        message: String,
    },
    Goodbye,
}

pub fn write_message(mut writer: impl Write, message: &Message) -> io::Result<()> {
    let payload = encode_message(message)?;
    if payload.len() > MAX_FRAME_SIZE {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "protocol frame exceeds the size limit",
        ));
    }
    let payload_len = payload.len() as u32;
    writer.write_all(&payload_len.to_be_bytes())?;
    writer.write_all(&payload)
}

pub fn read_message(mut reader: impl Read) -> io::Result<Message> {
    let mut length = [0_u8; 4];
    reader.read_exact(&mut length)?;
    let length = u32::from_be_bytes(length) as usize;
    if length > MAX_FRAME_SIZE {
        return Err(invalid_data("protocol frame exceeds the size limit"));
    }
    let mut payload = vec![0; length];
    reader.read_exact(&mut payload)?;
    decode_message(&payload)
}

fn encode_message(message: &Message) -> io::Result<Vec<u8>> {
    rmp_serde::to_vec_named(message)
        .map_err(|error| invalid_data(format!("failed to encode MessagePack message: {error}")))
}

fn decode_message(payload: &[u8]) -> io::Result<Message> {
    let mut decoder = rmp_serde::Deserializer::new(io::Cursor::new(payload));
    let message = Message::deserialize(&mut decoder)
        .map_err(|error| invalid_data(format!("failed to decode MessagePack message: {error}")))?;
    if decoder.position() != payload.len() as u64 {
        return Err(invalid_data("trailing bytes after MessagePack message"));
    }
    Ok(message)
}

fn invalid_data(message: impl Into<String>) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidData, message.into())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn all_messages_round_trip() {
        let messages = [
            Message::Hello {
                version: PROTOCOL_VERSION,
                capabilities: CAPABILITY_READ_IMAGES,
            },
            Message::ImageOffer(ImageOffer {
                id: 42,
                name: "café.png".to_owned(),
                remote_path: "/tmp/café.png".to_owned(),
                width: Some(640),
                height: Some(480),
                format_hint: Some("png".to_owned()),
            }),
            Message::ImageOffer(ImageOffer {
                id: 43,
                name: "extensionless".to_owned(),
                remote_path: "/tmp/extensionless".to_owned(),
                width: None,
                height: None,
                format_hint: None,
            }),
            Message::RequestImageData { id: 42 },
            Message::ImageData {
                id: 42,
                encoded_bytes: vec![0, 1, 2, 255],
            },
            Message::CommandResult {
                request_id: 9,
                succeeded: false,
                message: "not supported".to_owned(),
            },
            Message::Error {
                id: Some(42),
                message: "missing".to_owned(),
            },
            Message::Goodbye,
        ];

        for message in messages {
            let mut bytes = Vec::new();
            write_message(&mut bytes, &message).unwrap();
            assert_eq!(read_message(bytes.as_slice()).unwrap(), message);
        }
    }

    #[test]
    fn rejects_oversized_frame_before_allocating() {
        let length = (MAX_FRAME_SIZE as u32 + 1).to_be_bytes();
        let error = read_message(length.as_slice()).unwrap_err();
        assert_eq!(error.kind(), io::ErrorKind::InvalidData);
    }

    #[test]
    fn rejects_trailing_bytes() {
        let mut payload = encode_message(&Message::Goodbye).unwrap();
        payload.push(0);
        assert!(decode_message(&payload).is_err());
    }

    #[test]
    fn hello_has_stable_named_messagepack_encoding() {
        let payload = encode_message(&Message::Hello {
            version: 1,
            capabilities: CAPABILITY_READ_IMAGES,
        })
        .unwrap();

        assert_eq!(
            payload,
            b"\x82\xa4type\xa5hello\xa4body\x82\xa7version\x01\xaccapabilities\x01"
        );
    }

    #[test]
    fn image_data_uses_messagepack_binary_encoding() {
        let payload = encode_message(&Message::ImageData {
            id: 42,
            encoded_bytes: vec![0, 1, 2, 255],
        })
        .unwrap();

        assert!(payload.windows(6).any(|bytes| bytes == b"\xc4\x04\0\x01\x02\xff"));
    }
}
