import csv
import numpy as np
import pandas as pd

# Timestamps data
def input_file_listener(percorso_file):
    data = []
    i = 0
    with open(percorso_file, 'r') as file:
        reader = csv.reader(file)
        for riga in reader:
            try:
                data.append(riga)
                i += 1
            except ValueError:
                print(f"Riga non valida: {riga}")
    print(f"Righe listener caricate: {i}")
    return data

def input_file_talker(percorso_file):
    data = []
    i = 0
    with open(percorso_file, 'r') as file:
        reader = csv.reader(file)
        for riga in reader:
            try:

                data.append([riga[0], riga[3]])
                i += 1
            except ValueError:
                print(f"Riga non valida: {riga}")
    print(f"Righe listener caricate: {i}")
    return data

# Convert to pandas DataFrame
listener_log = input_file_listener("listener_log.csv")
talker_log = input_file_talker("talker_log.csv")
df1 = pd.DataFrame(listener_log, columns=["ID", "Timestamp_RX"]).astype({"ID": int, "Timestamp_RX": int})
df2 = pd.DataFrame(talker_log, columns=["ID", "Timestamp_TX"]).astype({"ID": int, "Timestamp_TX": int})
# Merge on the ID to find matching timestamps
merged_df = pd.merge(df1, df2, on="ID")

# Rimuove le prime N righe dopo il merge
N = 10
merged_df = merged_df.iloc[N:]

# Calcolo del Delay e del Jitter
merged_df["Delay"] = merged_df["Timestamp_RX"] - merged_df["Timestamp_TX"]
merged_df["Jitter"] = merged_df["Delay"].diff().abs().fillna(0)

# Calcolo delle statistiche
latency_min = merged_df["Delay"].min()
latency_max = merged_df["Delay"].max()
latency_mean = merged_df["Delay"].mean()
latency_std = merged_df["Delay"].std()

jitter_min = merged_df["Jitter"].min()
jitter_max = merged_df["Jitter"].max()
jitter_mean = merged_df["Jitter"].mean()
jitter_std = merged_df["Jitter"].std()

# Salvataggio dei risultati
output_file = "results_VM.txt"
with open(output_file, "w") as file:
    file.write(merged_df.to_string(index=False))
    file.write("\n\nStatistiche:\n")
    file.write("====================================================================================================\n")
    file.write(f"Latenza minima: {latency_min} ns\n")
    file.write(f"Latenza massima: {latency_max} ns\n")
    file.write(f"Latenza media: {latency_mean:.2f} ns\n")
    file.write(f"Deviazione standard latenza: {latency_std:.2f} ns\n\n")
    file.write(f"Jitter minimo: {jitter_min} ns\n")
    file.write(f"Jitter massimo: {jitter_max} ns\n")
    file.write(f"Jitter medio: {jitter_mean:.2f} ns\n")
    file.write(f"Deviazione standard jitter: {jitter_std:.2f} ns\n")

# Mostra i risultati
print(merged_df[["ID", "Timestamp_TX", "Timestamp_RX", "Delay", "Jitter"]])
print("\nStatistiche:")
print(f"Latenza minima: {latency_min} ns")
print(f"Latenza massima: {latency_max} ns")
print(f"Latenza media: {latency_mean:.2f} ns")
print(f"Deviazione standard latenza: {latency_std:.2f} ns")
print(f"Jitter minimo: {jitter_min} ns")
print(f"Jitter massimo: {jitter_max} ns")
print(f"Jitter medio: {jitter_mean:.2f} ns")
print(f"Deviazione standard jitter: {jitter_std:.2f} ns")
print(f"Risultati salvati in: {output_file}")

# Solo out per plot:
output_file = "output.txt"
with open(output_file, "w") as file:
    file.write(merged_df.to_string(index=False))