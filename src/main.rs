/**
 * Распределённая система телеметрии датчика температуры
 * и управления работой кулера
 * Серверное приложение на языке Rust
 * ГомГТУ им. П.О. Сухого, 2026
 */

use anyhow::Result;
use chrono::Local;
use rumqttc::{Client, MqttOptions, QoS, Event, Packet};
use serde::Deserialize;
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;

// ================= СТРУКТУРЫ ДАННЫХ =================

/// Структура телеметрических данных, получаемых от ESP32
#[derive(Debug, Deserialize, Clone)]
struct TelemetryData {
    device_id: String,          // Идентификатор устройства
    temperature_c: f64,         // Температура в градусах Цельсия
    fan_state: bool,            // Состояние вентилятора
    threshold: f64,             // Пороговое значение температуры
    timestamp: u64,             // Время работы устройства (мс)
}

/// Структура для расчёта статистики температуры
#[derive(Debug, Clone)]
struct TemperatureStats {
    min: f64,                   // Минимальная температура
    max: f64,                   // Максимальная температура
    avg: f64,                   // Скользящее среднее
    count: usize,               // Количество полученных пакетов
    last_temp: f64,             // Последнее измеренное значение
}

impl TemperatureStats {
    /// Создание новой структуры статистики
    fn new() -> Self {
        Self {
            min: f64::MAX,
            max: f64::MIN,
            avg: 0.0,
            count: 0,
            last_temp: 0.0,
        }
    }

    /// Обновление статистики новым значением температуры
    fn update(&mut self, temp: f64) {
        self.min = self.min.min(temp);
        self.max = self.max.max(temp);
        self.last_temp = temp;
        self.count += 1;
        
        // Расчёт экспоненциального скользящего среднего
        let alpha = 0.1;
        self.avg = if self.count == 1 {
            temp
        } else {
            alpha * temp + (1.0 - alpha) * self.avg
        };
    }
}

// ================= ФУНКЦИИ MQTT КЛИЕНТА =================

/// Задача MQTT клиента для приёма телеметрических данных
fn mqtt_client_task(stats: Arc<Mutex<TemperatureStats>>) -> Result<()> {
    // Настройка подключения к MQTT брокеру
    let mut mqttoptions = MqttOptions::new("rust-telemetry-server", "172.20.10.14", 1883);
    mqttoptions.set_keep_alive(Duration::from_secs(5));
    mqttoptions.set_clean_session(true);

    let (mut client, mut eventloop) = Client::new(mqttoptions, 10);
    
    // Подписка на топик телеметрии
    client.subscribe("telemetry/temperature", QoS::AtMostOnce)?;
    println!("✅ Подписка на 'telemetry/temperature' активна");
    println!(" Сервер телеметрии запущен...\n");

    // Главный цикл обработки событий MQTT
    for notification in eventloop.iter() {
        if let Ok(Event::Incoming(Packet::Publish(msg))) = notification {
            // Парсинг JSON сообщения
            match serde_json::from_slice::<TelemetryData>(&msg.payload) {
                Ok(data) => {
                    // Обновление статистики
                    {
                        let mut stats = stats.lock().unwrap();
                        stats.update(data.temperature_c);
                    }
                    
                    // Формирование строки состояния вентилятора
                    let fan_status = if data.fan_state { "ON " } else { "OFF " };
                    
                    // Вывод данных в консоль
                    println!(
                        "📥 [{}] {} | Uptime: {:.1}s | {:.1}°C | Fan: {} | Threshold: {:.0}°C",
                        data.device_id,
                        Local::now().format("%H:%M:%S"),
                        data.timestamp as f64 / 1000.0,
                        data.temperature_c,
                        fan_status,
                        data.threshold
                    );
                }
                Err(e) => {
                    eprintln!("⚠️  Ошибка парсинга JSON: {}", e);
                }
            }
        }
    }
    Ok(())
}

// ================= ФУНКЦИИ СТАТИСТИКИ =================

/// Задача вывода статистики каждые 10 секунд
fn stats_task(stats: Arc<Mutex<TemperatureStats>>) {
    println!("\n === СТАТИСТИКА ТЕМПЕРАТУРЫ ===");
    println!("Формат: [Время] | Последняя | Мин | Макс | Среднее | Записей");
    println!("─────────────────────────────────────────────────────────────\n");
    
    loop {
        thread::sleep(Duration::from_secs(10));
        
        let stats = stats.lock().unwrap();
        if stats.count > 0 {
            println!(
                " [{}] | {:.1}°C | {:.1}°C | {:.1}°C | {:.1}°C | {}",
                Local::now().format("%H:%M:%S"),
                stats.last_temp,
                stats.min,
                stats.max,
                stats.avg,
                stats.count
            );
        }
    }
}

// ================= ОСНОВНАЯ ФУНКЦИЯ =================

#[tokio::main]
async fn main() -> Result<()> {
    println!(" Запуск сервера телеметрии на Rust...");
    println!(" Подключение к MQTT брокеру 172.20.10.14:1883...\n");

    // Инициализация структуры статистики
    let stats = Arc::new(Mutex::new(TemperatureStats::new()));
    
    // Запуск MQTT клиента в отдельном потоке
    let stats_clone = Arc::clone(&stats);
    let mqtt_handle = thread::spawn(move || {
        if let Err(e) = mqtt_client_task(stats_clone) {
            eprintln!("❌ Ошибка MQTT клиента: {}", e);
        }
    });
    
    // Запуск задачи вывода статистики в отдельном потоке
    let stats_clone = Arc::clone(&stats);
    let stats_handle = thread::spawn(move || {
        stats_task(stats_clone);
    });
    
    // Ожидание завершения потоков (бесконечный цикл)
    let _ = mqtt_handle.join();
    let _ = stats_handle.join();
    
    Ok(())
}