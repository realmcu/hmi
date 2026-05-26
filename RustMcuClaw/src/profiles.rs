use std::{fs, path::Path};

use anyhow::Result;
use rustmcuclaw_common::format_pc_system_prompt;

use crate::config::FileConfig;

#[derive(Debug, Clone)]
pub struct Profiles {
    pub soul: String,
    pub user: String,
    pub role: String,
}

impl Profiles {
    pub fn load(files: &FileConfig) -> Result<Self> {
        Ok(Self {
            soul: read_or_empty(&files.soul)?,
            user: read_or_empty(&files.user)?,
            role: read_or_empty(&files.role)?,
        })
    }

    pub fn system_prompt(&self, summary_memories: &str) -> String {
        format_pc_system_prompt(&self.soul, &self.user, &self.role, summary_memories)
    }
}

fn read_or_empty(path: &Path) -> Result<String> {
    if path.exists() {
        Ok(fs::read_to_string(path)?)
    } else {
        Ok(String::new())
    }
}

