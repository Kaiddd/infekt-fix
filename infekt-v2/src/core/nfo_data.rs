// This is a rust-native wrapper around the C++ and FFI stuff.
// Later, this can become a rust-native implementation :-)

use cxx::{let_cxx_string, UniquePtr};
use std::path::{Path, PathBuf};

use super::cpp::{ffi, ffi::ENfoCharset};
use super::nfo_renderer_grid::{make_renderer_grid, NfoRendererGrid};
use super::nfo_to_html::nfo_to_html_classic;

pub struct NfoData {
    nfo: UniquePtr<ffi::CNFOData>,
    renderer_grid: Option<NfoRendererGrid>,
    file_path: Option<PathBuf>,
    cached_classic_text: Option<String>,
    cached_stripped_text: Option<String>,
}

impl NfoData {
    pub fn new() -> NfoData {
        NfoData {
            nfo: (UniquePtr::null()),
            renderer_grid: None,
            file_path: None,
            cached_classic_text: None,
            cached_stripped_text: None,
        }
    }

    pub fn is_loaded(&self) -> bool {
        !self.nfo.is_null()
    }

    pub fn has_blocks(&self) -> bool {
        self.renderer_grid
            .as_ref()
            .map_or(false, |grid| grid.has_blocks)
    }

    pub fn get_file_path(&self) -> Option<&Path> {
        self.file_path.as_deref()
    }

    pub fn get_file_name(&self) -> Option<String> {
        self.file_path.as_ref().map(|p| {
            p.file_name()
                .unwrap_or_default()
                .to_string_lossy()
                .to_string()
        })
    }

    pub fn load_from_file(&mut self, path: &Path) -> Result<(), String> {
        let mut load_nfo = ffi::new_nfo_data();
        let_cxx_string!(path_cxx = String::from(path.to_string_lossy()));

        if load_nfo.pin_mut().LoadFromFileUtf8(&path_cxx) {
            self.nfo = load_nfo;
            self.file_path = Some(path.to_path_buf());
            self.renderer_grid = Some(make_renderer_grid(&self.nfo));

            self.cached_classic_text =
                Some(cpp_char_vector_to_utf8_string(self.nfo.GetContentsUint32()));
            self.cached_stripped_text = self.make_stripped_text();

            return Ok(());
        }

        Err(load_nfo.pin_mut().GetLastErrorDescription().to_string())
    }

    pub fn get_charset_name(&self) -> &str {
        if self.nfo.is_null() {
            return "(none)";
        }

        match self.nfo.GetCharset() {
            ENfoCharset::NFOC_UTF16 => "UTF-16",
            ENfoCharset::NFOC_UTF8_SIG => "UTF-8 (Signature)",
            ENfoCharset::NFOC_UTF8 => "UTF-8",
            ENfoCharset::NFOC_CP437 => "CP 437",
            ENfoCharset::NFOC_CP437_IN_UTF8 => "CP 437 (in UTF-8)",
            ENfoCharset::NFOC_CP437_IN_UTF16 => "CP 437 (in UTF-16)",
            ENfoCharset::NFOC_CP437_STRICT => "CP 437 (strict mode)",
            ENfoCharset::NFOC_WINDOWS_1252 => "Windows-1252",
            ENfoCharset::NFOC_CP437_IN_CP437 => "CP 437 (double encoded)",
            ENfoCharset::NFOC_CP437_IN_CP437_IN_UTF8 => "CP 437 (double encoded + UTF-8)",
            _ => "(unknown)",
        }
    }

    pub fn get_renderer_grid(&self) -> Option<&NfoRendererGrid> {
        if self.nfo.is_null() {
            return None;
        }

        self.renderer_grid.as_ref()
    }

    pub fn get_classic_html(&self) -> String {
        if self.nfo.is_null() {
            return String::new();
        }

        nfo_to_html_classic(&self.nfo)
    }

    pub fn get_classic_text(&self) -> String {
        if self.nfo.is_null() {
            return String::new();
        }

        self.cached_classic_text.clone().unwrap_or_default()
    }

    pub fn get_stripped_text(&self) -> String {
        if self.nfo.is_null() {
            return String::new();
        }

        self.cached_stripped_text.clone().unwrap_or_default()
    }

    fn make_stripped_text(&mut self) -> Option<String> {
        if !self.nfo.is_null() {
            let mut load_stripped_nfo = ffi::new_nfo_data();

            if load_stripped_nfo.pin_mut().LoadStripped(&self.nfo) {
                return Some(cpp_char_vector_to_utf8_string(
                    load_stripped_nfo.GetContentsUint32(),
                ));
            }
        }

        None
    }
}

impl Default for NfoData {
    fn default() -> Self {
        Self::new()
    }
}

fn cpp_char_vector_to_utf8_string(vector: Vec<u32>) -> String {
    vector
        .iter()
        .map(|&c| char::from_u32(c).unwrap_or(char::REPLACEMENT_CHARACTER))
        .collect()
}
