import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns

# Definisci i file di input e i nomi delle categorie
files = {
    "KuberneTSN": "output_dpdk_64.txt",
    "Flannel": "output_flannel_64.txt"
}

# Legge e unisce i dati
df_list = []
for system, file in files.items():
    df = pd.read_csv(file, sep=r'\s+')
    df["Sistema"] = system  # Aggiunge una colonna per distinguere i due sistemi
    df_list.append(df)

df_combined = pd.concat(df_list, ignore_index=True)

# Ristrutturazione per avere una colonna "Metrica" con valori Delay/Jitter
df_melted = df_combined.melt(id_vars=["Sistema"], value_vars=["Delay", "Jitter"],
                             var_name="Metrica", value_name="Valore")

# Converte i valori da nanosecondi a microsecondi
df_melted["Valore"] = df_melted["Valore"] / 1000

# Palette di colori personalizzata
palette = {"KuberneTSN": "#1f77b4", "Flannel": "#ff7f0e"}  # Blu e Arancione

# Creazione del grafico combinato
plt.figure(figsize=(10, 6))
sns.boxplot(x="Metrica", y="Valore", hue="Sistema", data=df_melted, showfliers=False, palette=palette)

# Titoli e labels
plt.title("Confronto della distribuzione di Delay e Jitter")
#plt.ylabel("Valore (ns)")
plt.ylabel("Valore (µs)")

plt.xlabel("Metrica su 1000 pacchetti e Payload di 1024 byte")
plt.legend(title="Sistema virtuale")

# Salvataggio e visualizzazione
plt.savefig("boxplot_comparativo.png", dpi=300)
plt.show()
