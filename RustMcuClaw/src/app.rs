use std::{
    fs,
    sync::{Arc, Mutex},
};

use anyhow::Result;
use rustmcuclaw_common::format_task_chat_input;
use rustyline::{error::ReadlineError, DefaultEditor};
use tokio::time;

use crate::{
    config::Config,
    llm::{LlmClient, Message},
    memory::MemoryStore,
    profiles::Profiles,
    tasks::{Task, TaskStore, MIN_TASK_INTERVAL_SECS},
};

pub struct AssistantApp {
    config: Arc<Config>,
    llm: LlmClient,
    memory: MemoryStore,
    tasks: TaskStore,
}

enum CommandOutcome {
    NotHandled,
    Handled,
    Exit,
}

impl AssistantApp {
    pub async fn new(config: Config) -> Result<Self> {
        config.ensure_files()?;
        let llm = LlmClient::new(config.provider.clone(), config.api_key());
        let memory = MemoryStore::new(config.files.summary_memory.clone());
        let tasks = TaskStore::load(config.files.tasks.clone()).await?;
        Ok(Self {
            config: Arc::new(config),
            llm,
            memory,
            tasks,
        })
    }

    pub async fn run_chat(&self) -> Result<()> {
        self.spawn_heartbeat();

        println!("RustMcuClaw terminal chat. 输入 /help 查看命令，/exit 退出。");
        let mut rl = DefaultEditor::new()?;
        let history = Arc::new(Mutex::new(Vec::<Message>::new()));
        let session_history = Arc::new(Mutex::new(Vec::<Message>::new()));
        let task_runner = self.spawn_task_chat_loop(history.clone(), session_history.clone());

        loop {
            match rl.readline("you> ") {
                Ok(line) => {
                    let input = line.trim();
                    if input.is_empty() {
                        continue;
                    }
                    let _ = rl.add_history_entry(input);
                    match self.handle_command(input).await? {
                        CommandOutcome::Handled => continue,
                        CommandOutcome::Exit => break,
                        CommandOutcome::NotHandled => {}
                    }

                    let answer = self.answer_from_shared(input, &history, &session_history).await?;
                    println!("assistant> {}", answer);
                }
                Err(ReadlineError::Interrupted | ReadlineError::Eof) => break,
                Err(err) => return Err(err.into()),
            }
        }

        task_runner.abort();

        let final_session_history = snapshot_messages(&session_history);
        if let Err(err) = self.persist_session_summary(&final_session_history).await {
            eprintln!("warning: failed to save summary memory: {err}");
        }
        Ok(())
    }

    pub async fn ask_once(&self, prompt: &str) -> Result<()> {
        let answer = self.answer(prompt, &[]).await?;
        println!("{}", answer);
        let history = vec![Message::user(prompt), Message::assistant(&answer)];
        if let Err(err) = self.persist_session_summary(&history).await {
            eprintln!("warning: failed to save summary memory: {err}");
        }
        Ok(())
    }

    pub async fn add_task(&self, title: &str, every_secs: u64) -> Result<()> {
        let task = self.tasks.add(title, every_secs).await?;
        println!(
            "added task {}: {} every {}s",
            &task.id.to_string()[..8],
            task.title,
            task.every_secs
        );
        Ok(())
    }

    pub async fn list_tasks(&self) -> Result<()> {
        for task in self.tasks.list().await {
            println!(
                "{} [{}] every={}s next={}",
                &task.id.to_string()[..8],
                if task.completed { "done" } else { "active" },
                task.every_secs,
                task.next_run_at
            );
        }
        Ok(())
    }

    pub async fn complete_task(&self, id: &str) -> Result<()> {
        if self.tasks.complete_prefix(id).await? {
            println!("task completed");
        } else {
            println!("task not found");
        }
        Ok(())
    }

    pub async fn doctor(&self) -> Result<()> {
        println!("data_dir: {}", self.config.data_dir.display());
        println!("provider: {:?}", self.config.provider.kind);
        println!("model: {}", self.config.provider.model);
        println!("endpoint: {}", self.config.endpoint());
        println!(
            "api_key_env: {} ({})",
            redact_api_key_label(&self.config.provider.api_key_env),
            if self.config.api_key().is_some() {
                "set"
            } else {
                "missing"
            }
        );
        println!("soul: {}", self.config.files.soul.display());
        println!("user: {}", self.config.files.user.display());
        println!("role: {}", self.config.files.role.display());
        println!(
            "summary_memory: {}",
            self.config.files.summary_memory.display()
        );
        println!("tasks: {}", self.config.files.tasks.display());
        Ok(())
    }

    pub async fn init_files(&self) -> Result<()> {
        self.config.ensure_files()?;
        fs::create_dir_all("config")?;
        let example = include_str!("../config/rmcc.example.toml");
        if !std::path::Path::new("config/rmcc.toml").exists() {
            fs::write("config/rmcc.toml", example)?;
        }
        println!("initialized config and data files");
        Ok(())
    }

    async fn answer(&self, input: &str, history: &[Message]) -> Result<String> {
        Self::answer_with(self.config.clone(), self.llm.clone(), self.memory.clone(), input, history)
            .await
    }

    async fn answer_with(
        config: Arc<Config>,
        llm: LlmClient,
        memory: MemoryStore,
        input: &str,
        history: &[Message],
    ) -> Result<String> {
        let profiles = Profiles::load(&config.files)?;
        let summary_memories = memory.render_prompt_text()?;
        let system = profiles.system_prompt(&summary_memories);

        let mut messages = vec![Message::system(system)];
        messages.extend_from_slice(history);
        messages.push(Message::user(input));
        llm.chat(&config.endpoint(), &messages).await
    }

    async fn handle_command(&self, input: &str) -> Result<CommandOutcome> {
        let parts: Vec<&str> = input.split_whitespace().collect();
        match parts.as_slice() {
            ["/exit"] | ["/quit"] => Ok(CommandOutcome::Exit),
            ["/help"] => {
                println!(
                    "/help | /exit | /doctor | /task list | /task add <seconds>=min{} <title> | /task done <id>",
                    MIN_TASK_INTERVAL_SECS
                );
                Ok(CommandOutcome::Handled)
            }
            ["/doctor"] => {
                self.doctor().await?;
                Ok(CommandOutcome::Handled)
            }
            ["/task", "list"] => {
                self.list_tasks().await?;
                Ok(CommandOutcome::Handled)
            }
            ["/task", "done", id] => {
                self.complete_task(id).await?;
                Ok(CommandOutcome::Handled)
            }
            ["/task", "add", secs, rest @ ..] if !rest.is_empty() => {
                let every_secs = secs.parse::<u64>()?;
                self.add_task(&rest.join(" "), every_secs).await?;
                Ok(CommandOutcome::Handled)
            }
            _ => Ok(CommandOutcome::NotHandled),
        }
    }

    async fn persist_session_summary(&self, history: &[Message]) -> Result<()> {
        if history.is_empty() {
            return Ok(());
        }

        let transcript = render_session_transcript(history);
        let summary = self
            .llm
            .summarize_dialogue(&self.config.endpoint(), &transcript)
            .await?;
        self.memory.append_summary(&summary)
    }

    async fn answer_from_shared(
        &self,
        input: &str,
        history: &Arc<Mutex<Vec<Message>>>,
        session_history: &Arc<Mutex<Vec<Message>>>,
    ) -> Result<String> {
        let history_snapshot = snapshot_messages(history);
        let answer = self.answer(input, &history_snapshot).await?;
        push_conversation_turn(
            history,
            session_history,
            self.config.chat.history_limit,
            input,
            &answer,
        );
        Ok(answer)
    }

    fn spawn_task_chat_loop(
        &self,
        history: Arc<Mutex<Vec<Message>>>,
        session_history: Arc<Mutex<Vec<Message>>>,
    ) -> tokio::task::JoinHandle<()> {
        let config = self.config.clone();
        let llm = self.llm.clone();
        let memory = self.memory.clone();
        let tasks = self.tasks.clone();

        tokio::spawn(async move {
            let mut ticker = time::interval(time::Duration::from_secs(1));
            loop {
                ticker.tick().await;
                match tasks.poll_due().await {
                    Ok(due_tasks) => {
                        for task in due_tasks {
                            if let Err(err) = run_due_task_chat(
                                &config,
                                &llm,
                                &memory,
                                &history,
                                &session_history,
                                task,
                            )
                            .await
                            {
                                eprintln!("[task] auto chat failed: {err}");
                            }
                        }
                    }
                    Err(err) => eprintln!("[task] {err}"),
                }
            }
        })
    }

    fn spawn_heartbeat(&self) {
        if !self.config.heartbeat.enabled {
            return;
        }
        let interval_secs = self.config.heartbeat.interval_secs;
        tokio::spawn(async move {
            let mut ticker = time::interval(time::Duration::from_secs(interval_secs));
            loop {
                ticker.tick().await;
                println!("\n[heartbeat] alive");
            }
        });
    }
}

async fn run_due_task_chat(
    config: &Arc<Config>,
    llm: &LlmClient,
    memory: &MemoryStore,
    history: &Arc<Mutex<Vec<Message>>>,
    session_history: &Arc<Mutex<Vec<Message>>>,
    task: Task,
) -> Result<()> {
    let task_title = task.title;
    let input = format_task_chat_input(&task_title);
    let history_snapshot = snapshot_messages(history);
    let answer = AssistantApp::answer_with(
        config.clone(),
        llm.clone(),
        memory.clone(),
        &input,
        &history_snapshot,
    )
    .await?;

    println!(
        "\n[task chat] {} ({})",
        task_title,
        &task.id.to_string()[..8]
    );
    println!("assistant> {}", answer);

    push_conversation_turn(
        history,
        session_history,
        config.chat.history_limit,
        &input,
        &answer,
    );
    Ok(())
}

fn trim_history(history: &mut Vec<Message>, max_messages: usize) {
    if history.len() > max_messages {
        let drain_to = history.len() - max_messages;
        history.drain(0..drain_to);
    }
}

fn snapshot_messages(history: &Arc<Mutex<Vec<Message>>>) -> Vec<Message> {
    history.lock().expect("history mutex poisoned").clone()
}

fn push_conversation_turn(
    history: &Arc<Mutex<Vec<Message>>>,
    session_history: &Arc<Mutex<Vec<Message>>>,
    history_limit: usize,
    input: &str,
    answer: &str,
) {
    let user_message = Message::user(input);
    let assistant_message = Message::assistant(answer);

    {
        let mut shared_history = history.lock().expect("history mutex poisoned");
        shared_history.push(user_message.clone());
        shared_history.push(assistant_message.clone());
        trim_history(&mut shared_history, history_limit);
    }

    let mut shared_session_history = session_history
        .lock()
        .expect("session history mutex poisoned");
    shared_session_history.push(user_message);
    shared_session_history.push(assistant_message);
}

fn render_session_transcript(history: &[Message]) -> String {
    history
        .iter()
        .map(|message| match message.role {
            crate::llm::Role::System => format!("system: {}", message.content),
            crate::llm::Role::User => format!("user: {}", message.content),
            crate::llm::Role::Assistant => format!("assistant: {}", message.content),
        })
        .collect::<Vec<_>>()
        .join("\n")
}

fn redact_api_key_label(value: &str) -> &str {
    let value = value.trim();
    if value.starts_with("sk-")
        || value.starts_with("gsk_")
        || value.starts_with("ghp_")
        || value.starts_with("github_pat_")
    {
        "<redacted-direct-key>"
    } else {
        value
    }
}
