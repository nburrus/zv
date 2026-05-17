#[derive(Clone, Debug)]
pub struct RgbaImage {
    width: u32,
    height: u32,
    bytes_per_row: usize,
    pixels: Vec<u8>,
}

impl RgbaImage {
    pub const BYTES_PER_PIXEL: usize = 4;
    pub const ROW_ALIGNMENT: usize = 256;

    pub fn new(width: u32, height: u32) -> Self {
        let bytes_per_row = aligned_bytes_per_row(width);
        let pixels = vec![0; bytes_per_row * height as usize];
        Self {
            width,
            height,
            bytes_per_row,
            pixels,
        }
    }

    pub fn from_tightly_packed_rgba(width: u32, height: u32, input: &[u8]) -> Self {
        let tight_bytes_per_row = width as usize * Self::BYTES_PER_PIXEL;
        assert_eq!(input.len(), tight_bytes_per_row * height as usize);

        let mut image = Self::new(width, height);
        for row in 0..height as usize {
            let src_start = row * tight_bytes_per_row;
            let dst_start = row * image.bytes_per_row;
            image.pixels[dst_start..dst_start + tight_bytes_per_row]
                .copy_from_slice(&input[src_start..src_start + tight_bytes_per_row]);
        }
        image
    }

    pub fn width(&self) -> u32 {
        self.width
    }

    pub fn height(&self) -> u32 {
        self.height
    }

    pub fn bytes_per_row(&self) -> usize {
        self.bytes_per_row
    }

    #[allow(dead_code)]
    pub fn pixels(&self) -> &[u8] {
        &self.pixels
    }

    pub fn pixels_mut(&mut self) -> &mut [u8] {
        &mut self.pixels
    }

    #[allow(dead_code)]
    pub fn row(&self, row: usize) -> &[u8] {
        let start = row * self.bytes_per_row;
        &self.pixels[start..start + self.bytes_per_row]
    }

    pub fn pixel_rgba(&self, x: u32, y: u32) -> Option<[u8; 4]> {
        if x >= self.width || y >= self.height {
            return None;
        }

        let offset = y as usize * self.bytes_per_row + x as usize * Self::BYTES_PER_PIXEL;
        Some([
            self.pixels[offset],
            self.pixels[offset + 1],
            self.pixels[offset + 2],
            self.pixels[offset + 3],
        ])
    }
}

fn aligned_bytes_per_row(width: u32) -> usize {
    let tight = width as usize * RgbaImage::BYTES_PER_PIXEL;
    let alignment = RgbaImage::ROW_ALIGNMENT;
    tight.next_multiple_of(alignment)
}
