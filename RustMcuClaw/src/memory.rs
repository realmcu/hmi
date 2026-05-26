use std::{
    fs::{self, OpenOptions},
    io::Write,
    path::PathBuf,
};

use anyhow::Result;
use chrono::{DateTime, Utc};
use rustmcuclaw_common::{NO_LONG_TERM_MEMORY_TEXT, SUMMARY_MEMORY_EMPTY_TEXT};
use serde::{Deserialize, Serialize};
use uuid::Uuid;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SummaryMemoryRecord {
    pub id: Uuid,
    pub created_at: DateTime<Utc>,
    pub summary: String,
}

#[derive(Clone)]
pub struct MemoryStore {
    path: PathBuf,
}

impl MemoryStore {
    pub fn new(path: PathBuf) -> Self {
        Self { path }
    }

    pub fn append_summary(&self, summary: &str) -> Result<()> {
        if let Some(parent) = self.path.parent() {
            fs::create_dir_all(parent)?;
        }
        let normalized = normalize_summary(summary);
        let record = SummaryMemoryRecord {
            id: Uuid::new_v4(),
            created_at: Utc::now(),
            summary: if normalized.is_empty() {
                NO_LONG_TERM_MEMORY_TEXT.to_string()
            } else {
                normalized
            },
        };
        let line = serde_json::to_string(&record)?;
        let mut file = OpenOptions::new()
            .create(true)
            .append(true)
            .open(&self.path)?;
        writeln!(file, "{}", line)?;
        Ok(())
    }

    pub fn load_all(&self) -> Result<Vec<SummaryMemoryRecord>> {
        if !self.path.exists() {
            return Ok(Vec::new());
        }
        let raw = fs::read_to_string(&self.path)?;
        Ok(raw
            .lines()
            .filter_map(|line| serde_json::from_str(line).ok())
            .collect())
    }

    pub fn render_prompt_text(&self) -> Result<String> {
        let records = self.load_all()?;
        if records.is_empty() {
            return Ok(SUMMARY_MEMORY_EMPTY_TEXT.to_string());
        }

        Ok(records
            .into_iter()
            .map(|record| format!("- {}", record.summary))
            .collect::<Vec<_>>()
            .join("\n"))
    }
}

fn normalize_summary(summary: &str) -> String {
    let compact = summary
        .lines()
        .map(str::trim)
        .filter(|line| !line.is_empty())
        .collect::<Vec<_>>()
        .join(" ");
    let compact = compact
        .trim()
        .trim_matches(|c| matches!(c, '"' | '\'' | '“' | '”'));
    compact.chars().take(50).collect()
}
