const mqtt = require('mqtt');
const express = require('express');
const sqlite3 = require('sqlite3').verbose();
const path = require('path');

const app = express();
const db = new sqlite3.Database(path.join(__dirname, 'mqtt_data.db'));

// Create table if it doesn't exist
db.run(`CREATE TABLE IF NOT EXISTS mqtt_data (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    temperature REAL,
    humidity REAL,
    aqi INT,
    tvoc INT,
    eco2 INT,
    timestamp DATETIME DEFAULT (datetime('now','localtime'))
)`);

// MQTT client setup
const client = mqtt.connect('mqtts://b512d33fcbc8401cb8504a21cce778a1.s1.eu.hivemq.cloud:8883', {
    username: 'DBandWebserver', // Replace with your MQTT username
    password: 'Eksamen2026'  // Replace with your MQTT password
});

client.on('connect', () => {
    console.log('Connected to MQTT broker');
    client.subscribe('sensor/data', (err) => {
        if (err) {
            console.error('Subscription error:', err);
            }
        });
    });

client.on('message', (topic, message) => {
    try {
        console.log('Received message:', message.toString());
        const data = JSON.parse(message.toString());
        const temperature = parseFloat(data.temperature);
        const humidity = parseFloat(data.humidity);
        const aqi = parseInt(data.aqi);
        const tvoc = parseInt(data.tvoc);
        const eco2 = parseInt(data.eco2);

        if (
            isNaN(temperature) || 
            isNaN(humidity) || 
            isNaN(aqi) || 
            isNaN(tvoc) || 
            isNaN(eco2)
        ) { 
            console.error('Invalid data format:', data);
            return;
        }
        
        
        db.run(`INSERT INTO mqtt_data (temperature, humidity, aqi, tvoc, eco2) VALUES (?, ?, ?, ?, ?)`,
            [temperature, humidity, aqi, tvoc, eco2],
                function (err) {
                    if (err) {
                        console.error('Database insertion error:', err);
                    } else {
                        console.log('Data inserted with ID:', this.lastID);
                    }
                }
            );
    } catch (err) {
        console.error('Error processing message:', err);
    }
});

app.get('/data', (req, res) => {
    db.all('SELECT * FROM mqtt_data ORDER BY timestamp DESC', [], (err, rows) => {
        if (err) {
            console.error('Database query error:', err);
            res.status(500).send({ error: 'Database query error' });
        } else {
            res.json(rows);
        }
    });
});

app.use(express.static(path.join(__dirname,'public')));

app.listen(3000, () => {
    console.log('Server is running on http://localhost:3000');
});
