from flask import Flask, jsonify, render_template_string
import csv
import os
from datetime import datetime
import glob

app = Flask(__name__)

# Configuración - ajusta estas rutas según tu sistema
HWINFO_CSV_PATH = r"C:\Users\tello\OneDrive\Documentos\GitHub\nds-temps-viewer"  # Carpeta donde HWiNFO guarda los CSV
CSV_PATTERN = "log.CSV"  # Patrón de archivos CSV

def get_latest_csv():
    """Encuentra el archivo CSV más reciente en la carpeta"""
    try:
        csv_files = glob.glob(os.path.join(HWINFO_CSV_PATH, CSV_PATTERN))
        if not csv_files:
            print(f"⚠️ No se encontraron archivos CSV en {HWINFO_CSV_PATH}")
            return None
        latest = max(csv_files, key=os.path.getmtime)
        print(f"📄 Usando archivo: {latest}")
        return latest
    except Exception as e:
        print(f"❌ Error buscando CSV: {e}")
        return None

def parse_hwinfo_csv(csv_file):
    """Lee el CSV de HWiNFO y extrae las últimas temperaturas"""
    try:
        # HWiNFO usa Windows-1252 (Latin-1) por el símbolo de grado °
        with open(csv_file, 'r', encoding='latin-1') as f:
            reader = csv.reader(f)
            
            # Lee todas las filas
            rows = list(reader)
            
            if len(rows) < 2:
                print("⚠️ CSV vacío o sin datos")
                return None
            
            # Primera fila: nombres de sensores
            headers = rows[0]
            print(f"📊 Columnas encontradas: {len(headers)}")
            
            # Última fila con datos
            last_row = rows[-1]
            
            # Filtra solo las columnas de temperatura
            temps = {}
            for i, header in enumerate(headers):
                if i < len(last_row):
                    # Busca columnas que contengan "Temp" o "Temperature"
                    header_lower = header.lower()
                    if 'temp' in header_lower and 'temp' not in header_lower.split('[')[0].lower():
                        # Evita columnas que son solo etiquetas
                        continue
                    
                    if 'temp' in header_lower or '°c' in header_lower or 'celsius' in header_lower:
                        try:
                            value_str = last_row[i].strip()
                            if value_str:
                                value = float(value_str)
                                # Filtra valores razonables (0-150°C)
                                if 0 <= value <= 150:
                                    sensor_name = header.strip()
                                    temps[sensor_name] = round(value, 1)
                        except (ValueError, IndexError) as e:
                            continue
            
            print(f"✅ Temperaturas encontradas: {len(temps)}")
            return temps if temps else None
            
    except Exception as e:
        print(f"❌ Error leyendo CSV: {e}")
        import traceback
        traceback.print_exc()
        return None

@app.route('/api/temps')
def get_temps():
    """Endpoint API para obtener temperaturas en JSON"""
    csv_file = get_latest_csv()
    
    if not csv_file:
        return jsonify({
            "error": "No se encontró archivo CSV",
            "path": HWINFO_CSV_PATH
        }), 404
    
    temps = parse_hwinfo_csv(csv_file)
    
    if not temps:
        return jsonify({
            "error": "No se pudieron leer temperaturas del CSV",
            "file": csv_file
        }), 500
    
    return jsonify({
        "timestamp": datetime.now().isoformat(),
        "temperatures": temps,
        "count": len(temps)
    })

@app.route('/api/temps/simple')
def get_temps_simple():
    """Endpoint simplificado para NDS - solo valores principales"""
    csv_file = get_latest_csv()
    
    if not csv_file:
        return "ERROR: No CSV found", 404
    
    temps = parse_hwinfo_csv(csv_file)
    
    if not temps:
        return "ERROR: No temps", 500
    
    # Formato simple para la NDS (texto plano)
    output = []
    for sensor, temp in list(temps.items())[:8]:  # Máximo 8 sensores
        short_name = sensor[:20]
        output.append(f"{short_name}: {temp}C")
    
    return "\n".join(output), 200, {'Content-Type': 'text/plain; charset=utf-8'}

@app.route('/api/debug')
def debug_info():
    """Endpoint de debug para verificar configuración"""
    csv_file = get_latest_csv()
    
    info = {
        "csv_path": HWINFO_CSV_PATH,
        "csv_exists": os.path.exists(HWINFO_CSV_PATH),
        "csv_file": csv_file,
        "csv_files_found": glob.glob(os.path.join(HWINFO_CSV_PATH, CSV_PATTERN))
    }
    
    if csv_file:
        try:
            with open(csv_file, 'r', encoding='utf-8-sig') as f:
                reader = csv.reader(f)
                rows = list(reader)
                info["csv_rows"] = len(rows)
                info["csv_headers"] = rows[0][:10] if rows else []
                info["csv_last_row"] = rows[-1][:10] if len(rows) > 1 else []
        except Exception as e:
            info["csv_error"] = str(e)
    
    return jsonify(info)

@app.route('/')
def index():
    """Página web simple para pruebas"""
    html = """
    <!DOCTYPE html>
    <html>
    <head>
        <title>Monitor de Temperaturas</title>
        <meta charset="utf-8">
        <style>
            body { 
                font-family: monospace; 
                background: #1e1e1e; 
                color: #00ff00; 
                padding: 20px;
            }
            .temp-box { 
                border: 2px solid #00ff00; 
                padding: 10px; 
                margin: 10px 0;
            }
            h1 { color: #00ff00; }
            .timestamp { color: #ffaa00; }
            .error { 
                color: #ff0000; 
                border: 2px solid #ff0000;
                padding: 10px;
                margin: 10px 0;
            }
            .debug {
                color: #aaaaaa;
                font-size: 0.9em;
                margin-top: 20px;
            }
        </style>
    </head>
    <body>
        <h1>🌡️ Monitor de Temperaturas PC</h1>
        <div id="temps">Cargando...</div>
        <div id="debug" class="debug"></div>
        
        <script>
            function updateTemps() {
                fetch('/api/temps')
                    .then(r => {
                        if (!r.ok) {
                            return r.json().then(err => {
                                throw new Error(err.error || 'Error desconocido');
                            });
                        }
                        return r.json();
                    })
                    .then(data => {
                        console.log('Datos recibidos:', data);
                        
                        if (!data.temperatures || typeof data.temperatures !== 'object') {
                            throw new Error('Formato de datos incorrecto');
                        }
                        
                        let html = '<div class="timestamp">Actualizado: ' + 
                                   new Date(data.timestamp).toLocaleTimeString() + 
                                   ' (' + data.count + ' sensores)</div>';
                        
                        const entries = Object.entries(data.temperatures);
                        
                        if (entries.length === 0) {
                            html += '<div class="error">No se encontraron temperaturas</div>';
                        }
                        
                        for (let [sensor, temp] of entries) {
                            let color = temp > 80 ? '#ff0000' : 
                                       temp > 60 ? '#ffaa00' : '#00ff00';
                            html += `<div class="temp-box" style="border-color: ${color}; color: ${color};">
                                       ${sensor}: <strong>${temp}°C</strong>
                                     </div>`;
                        }
                        
                        document.getElementById('temps').innerHTML = html;
                    })
                    .catch(e => {
                        console.error('Error:', e);
                        document.getElementById('temps').innerHTML = 
                            '<div class="error">❌ Error: ' + e.message + '</div>' +
                            '<div class="error">Ver debug info abajo o en /api/debug</div>';
                    });
            }
            
            // Carga info de debug
            fetch('/api/debug')
                .then(r => r.json())
                .then(data => {
                    document.getElementById('debug').innerHTML = 
                        '<h3>Debug Info:</h3><pre>' + 
                        JSON.stringify(data, null, 2) + '</pre>';
                });
            
            // Actualiza cada 2 segundos
            updateTemps();
            setInterval(updateTemps, 2000);
        </script>
    </body>
    </html>
    """
    return render_template_string(html)

if __name__ == '__main__':
    print("=" * 50)
    print("🚀 Servidor de temperaturas iniciado")
    print("=" * 50)
    print(f"📂 Ruta CSV: {HWINFO_CSV_PATH}")
    print(f"📊 Web: http://localhost:5000")
    print(f"📡 API JSON: http://localhost:5000/api/temps")
    print(f"📱 API Simple (NDS): http://localhost:5000/api/temps/simple")
    print(f"🔍 Debug: http://localhost:5000/api/debug")
    print("=" * 50)
    
    # Verifica la configuración inicial
    if not os.path.exists(HWINFO_CSV_PATH):
        print(f"⚠️ ADVERTENCIA: La ruta {HWINFO_CSV_PATH} no existe!")
        print("   Ajusta HWINFO_CSV_PATH en el código")
    
    # Permite conexiones desde la red local (para que la NDS pueda acceder)
    app.run(host='0.0.0.0', port=5000, debug=True)