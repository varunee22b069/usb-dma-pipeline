import sys
import matplotlib
matplotlib.use('TkAgg')
import matplotlib.pyplot as plt
import numpy as np

sampling_freq = 150000
freq_range = 1000
k = 5

plt.ion()
fig, ax = plt.subplots()
bars = ax.bar([], [])
ax.set_xlim(0, sampling_freq / 2)
ax.set_ylim(0, 1000)

print("Waiting for input...", file=sys.stderr)

while True:
    line_in = sys.stdin.readline()
    if not line_in:
        break

    parts = line_in.strip().split()
    if len(parts) != 2 * k:
        continue

    try:
        freqs = np.array([float(parts[2 * i]) for i in range(k)])
        mags  = np.array([float(parts[2 * i + 1]) for i in range(k)])
        # print("Got:", freqs, mags, file=sys.stderr)

        ax.cla()
        ax.bar(freqs, mags, width=min(sampling_freq / 2,freq_range) / (20 * k))
        ax.set_xlim(0, min(sampling_freq / 2,freq_range))
        ax.set_ylim(0, max(5, np.max(mags) * 1.1))
        plt.pause(0.01)

    except ValueError:
        continue
