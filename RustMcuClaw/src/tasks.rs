use std::{fs, path::PathBuf, sync::Arc};

use anyhow::{bail, Result};
use chrono::{DateTime, Duration, Utc};
pub use rustmcuclaw_common::MIN_TASK_INTERVAL_SECS;
use serde::{Deserialize, Serialize};
use tokio::sync::Mutex;
use uuid::Uuid;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Task {
    pub id: Uuid,
    pub title: String,
    pub every_secs: u64,
    pub next_run_at: DateTime<Utc>,
    pub completed: bool,
}

#[derive(Clone)]
pub struct TaskStore {
    path: PathBuf,
    tasks: Arc<Mutex<Vec<Task>>>,
}

impl TaskStore {
    pub async fn load(path: PathBuf) -> Result<Self> {
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent)?;
        }
        let tasks = if path.exists() {
            serde_json::from_str(&fs::read_to_string(&path)?)?
        } else {
            Vec::new()
        };
        Ok(Self {
            path,
            tasks: Arc::new(Mutex::new(tasks)),
        })
    }

    pub async fn add(&self, title: &str, every_secs: u64) -> Result<Task> {
        if every_secs < MIN_TASK_INTERVAL_SECS {
            bail!(
                "task interval must be at least {} seconds (10 minutes)",
                MIN_TASK_INTERVAL_SECS
            );
        }

        let task = Task {
            id: Uuid::new_v4(),
            title: title.to_string(),
            every_secs,
            next_run_at: Utc::now() + Duration::seconds(every_secs as i64),
            completed: false,
        };
        let mut tasks = self.tasks.lock().await;
        tasks.push(task.clone());
        self.save_locked(&tasks)?;
        Ok(task)
    }

    pub async fn list(&self) -> Vec<Task> {
        self.tasks.lock().await.clone()
    }

    pub async fn complete_prefix(&self, id_prefix: &str) -> Result<bool> {
        let mut tasks = self.tasks.lock().await;
        let mut found = false;
        for task in tasks
            .iter_mut()
            .filter(|t| t.id.to_string().starts_with(id_prefix))
        {
            task.completed = true;
            found = true;
        }
        self.save_locked(&tasks)?;
        Ok(found)
    }

    pub async fn poll_due(&self) -> Result<Vec<Task>> {
        let mut tasks = self.tasks.lock().await;
        let now = Utc::now();
        let mut due = Vec::new();
        let mut changed = false;
        for task in tasks
            .iter_mut()
            .filter(|t| !t.completed && t.next_run_at <= now)
        {
            due.push(task.clone());
            task.next_run_at = now + Duration::seconds(task.every_secs as i64);
            changed = true;
        }
        if changed {
            self.save_locked(&tasks)?;
        }
        Ok(due)
    }

    fn save_locked(&self, tasks: &[Task]) -> Result<()> {
        fs::write(&self.path, serde_json::to_string_pretty(tasks)?)?;
        Ok(())
    }
}
