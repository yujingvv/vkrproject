% --- 数字助听器算法  ---

clear; clc; close all;

% ====================【 1. 用户配置区 】====================
% --- 输入/输出文件 ---
input_filename = 'sample.wav'; 
output_filename = 'processed_audio.wav'; 

% --- 去噪参数 ---
noise_profile_duration_ms = 200; 
wiener_alpha = 0.98;

% --- 移频核心参数 ---
SOURCE_FREQ_START = 4000; TARGET_FREQ_START = 2000;
SOURCE_FREQ_END   = 8000; TARGET_FREQ_END   = 4000;

% --- 智能控制与平滑处理参数 ---
hlr_threshold = 0.5;
enable_crossfade = true;
transition_length_ms = 32;

% --- STFT参数 ---
n_fft = 1024; hop_length = n_fft / 4; win = hamming(n_fft, 'periodic');
% ================================================================


% --- 1. 读取原始信号 ---
try
    [original_signal, fs] = audioread(input_filename);
    fprintf('成功读取音频: %s, 采样率: %d Hz\n', input_filename, fs);
catch ME
    fprintf('错误: 未找到音频文件 ''%s''。\n', input_filename); return;
end
if size(original_signal, 2) > 1, original_signal = original_signal(:, 1); end
original_signal = double(original_signal);


% ====================【 核心处理: 纯频域流程 】====================
fprintf('\n--- 核心处理: 纯频域流程 ---\n');

% --- 步骤1: STFT (一次变换) ---
[Zxx_noisy, f, ~] = stft(original_signal, fs, 'Window', win, 'OverlapLength', n_fft - hop_length, 'FFTLength', n_fft);

% --- 步骤2: 全频带去噪 (维纳滤波) ---
fprintf('步骤1: 正在应用维纳滤波进行全局去噪...\n');
noise_profile_frames = ceil(noise_profile_duration_ms / 1000 * fs / hop_length);
noise_power_spectrum = mean(abs(Zxx_noisy(:, 1:min(noise_profile_frames, end))).^2, 2);
Zxx_denoised = zeros(size(Zxx_noisy));
prev_clean_power_sq = noise_power_spectrum;
for i = 1:size(Zxx_noisy, 2)
    noisy_frame_power = abs(Zxx_noisy(:, i)).^2;
    snr_posteriori = noisy_frame_power ./ noise_power_spectrum;
    snr_apriori = wiener_alpha * (prev_clean_power_sq ./ noise_power_spectrum) + (1 - wiener_alpha) * max(snr_posteriori - 1, 0);
    wiener_gain = snr_apriori ./ (snr_apriori + 1);
    Zxx_denoised(:, i) = wiener_gain .* Zxx_noisy(:, i);
    prev_clean_power_sq = abs(Zxx_denoised(:, i)).^2;
end
fprintf('去噪完成，得到干净频谱。\n');

% --- 步骤3: 智能决策  ---
fprintf('步骤2: 正在干净频谱上生成控制决策...\n');
hlr_low_bins = find(f >= 1000 & f < 3000); 
hlr_high_bins = find(f >= SOURCE_FREQ_START & f < SOURCE_FREQ_END);
process_switch = zeros(1, size(Zxx_denoised, 2));
for i = 1:size(Zxx_denoised, 2)
    frame = Zxx_denoised(:, i);
    energy_low = sum(abs(frame(hlr_low_bins)).^2);
    energy_high = sum(abs(frame(hlr_high_bins)).^2);
    if energy_high > (hlr_threshold * energy_low)
        process_switch(i) = 1;
    end
end

% --- 步骤4: 平滑比例生成 ---
fprintf('步骤3: 正在生成平滑过渡比例...\n');
transition_length_frames = round(transition_length_ms / 1000 * fs / hop_length);
mix_ratio = double(process_switch); 
if enable_crossfade && transition_length_frames > 1
    diff_switch = diff([0, process_switch, 0]);
    onset_indices = find(diff_switch == 1);
    offset_indices = find(diff_switch == -1);
    fade_in_win = linspace(0, 1, transition_length_frames);
    fade_out_win = linspace(1, 0, transition_length_frames);
    for i = 1:length(onset_indices), start_idx = onset_indices(i); end_idx = min(start_idx + transition_length_frames - 1, length(mix_ratio)); actual_len = end_idx - start_idx + 1; mix_ratio(start_idx:end_idx) = max(mix_ratio(start_idx:end_idx), fade_in_win(1:actual_len)); end
    for i = 1:length(offset_indices), start_idx = offset_indices(i); end_idx = min(start_idx + transition_length_frames - 1, length(mix_ratio)); actual_len = end_idx - start_idx + 1; mix_ratio(start_idx:end_idx) = min(mix_ratio(start_idx:end_idx), fade_out_win(1:actual_len)); end
end

% --- 步骤5: 逐帧移频与平滑混合 ---
fprintf('步骤4: 正在逐帧进行移频与平滑混合...\n');
Zxx_final = zeros(size(Zxx_denoised));
target_bins = find(f >= TARGET_FREQ_START & f < TARGET_FREQ_END);
source_bins = find(f >= SOURCE_FREQ_START & f < SOURCE_FREQ_END);
source_freqs = f(source_bins); target_freqs = f(target_bins);
gamma = (TARGET_FREQ_END - TARGET_FREQ_START) / (SOURCE_FREQ_END - SOURCE_FREQ_START);

for i = 1:size(Zxx_denoised, 2)
    clean_frame = Zxx_denoised(:, i);
    transposed_frame = clean_frame;
    source_spectrum = clean_frame(source_bins);
    mapped_source_freqs = (target_freqs - TARGET_FREQ_START) / gamma + SOURCE_FREQ_START;
    interp_real = interp1(source_freqs, real(source_spectrum), mapped_source_freqs, 'linear', 'extrap');
    interp_imag = interp1(source_freqs, imag(source_spectrum), mapped_source_freqs, 'linear', 'extrap');
    transposed_frame(source_bins) = 0;
    transposed_frame(target_bins) = complex(interp_real, interp_imag);
    Zxx_final(:, i) = (1 - mix_ratio(i)) * clean_frame + mix_ratio(i) * transposed_frame;
end
fprintf('核心处理全部完成。\n');

% --- 步骤6: ISTFT (一次逆变换) ---
processed_signal = real(istft(Zxx_final, fs, 'Window', win, 'OverlapLength', n_fft - hop_length, 'FFTLength', n_fft));
if length(processed_signal) > length(original_signal), processed_signal = processed_signal(1:length(original_signal)); else, processed_signal(end+1:length(original_signal)) = 0; end


% ====================【 结果展示与保存 】====================
% --- 3. 幅度归一化 & 保存 ---
max_orig_amp = max(abs(original_signal));
max_proc_amp = max(abs(processed_signal));
if max_proc_amp > 1e-6, processed_signal = processed_signal * (max_orig_amp / max_proc_amp); end
audiowrite(output_filename, processed_signal, fs);
fprintf('\n最终处理后的音频已保存为: %s\n', output_filename);

% --- 4. 可视化对比 ---
figure('Name', '算法语谱图对比', 'Position', [100, 100, 900, 700]);
subplot(2, 1, 1); spectrogram(original_signal, win, n_fft-hop_length, n_fft, fs, 'yaxis'); title('原始音频');
subplot(2, 1, 2); spectrogram(processed_signal, win, n_fft-hop_length, n_fft, fs, 'yaxis'); title('最终处理后');
saveas(gcf, 'result_spectrograms.png');

% --- 5. 生成移频效果的特写图 ---
fprintf('正在生成移频效果的特写图...\n');
% 找到一个最适合展示的帧：即移频开关打开，且高频能量最大的帧
[~, best_frame_idx] = max(sum(abs(Zxx_denoised(hlr_high_bins, find(process_switch==1))).^2));
active_indices = find(process_switch==1);
target_frame_idx = active_indices(best_frame_idx);

frame_before = Zxx_denoised(:, target_frame_idx);
frame_after = Zxx_final(:, target_frame_idx);

figure('Name', '移频效果特写图', 'Position', [200, 200, 900, 500]);
plot(f/1000, 20*log10(abs(frame_before)), 'b-', 'LineWidth', 1.5, 'DisplayName', '处理前 (仅去噪)');
hold on;
plot(f/1000, 20*log10(abs(frame_after)), 'r-', 'LineWidth', 1.5, 'DisplayName', '处理后 (去噪+移频)');
grid on;
title(sprintf('单帧频谱对比 (在 %.2f 秒)', target_frame_idx * hop_length / fs));
xlabel('频率 (kHz)');
ylabel('幅度 (dB)');
legend('show', 'Location', 'southwest');
xlim([0 fs/2000]);
ylim([-100 50]); % 调整Y轴范围以便观察

% 添加标注
hold on;
patch([SOURCE_FREQ_START/1000, SOURCE_FREQ_END/1000, SOURCE_FREQ_END/1000, SOURCE_FREQ_START/1000], [ylim fliplr(ylim)], 'b', 'FaceAlpha', 0.1, 'EdgeColor', 'none', 'HandleVisibility', 'off');
patch([TARGET_FREQ_START/1000, TARGET_FREQ_END/1000, TARGET_FREQ_END/1000, TARGET_FREQ_START/1000], [ylim fliplr(ylim)], 'r', 'FaceAlpha', 0.1, 'EdgeColor', 'none', 'HandleVisibility', 'off');
text((SOURCE_FREQ_START+SOURCE_FREQ_END)/2000, -15, '源频段', 'Color', 'b', 'HorizontalAlignment', 'center', 'FontSize', 12);
text((TARGET_FREQ_START+TARGET_FREQ_END)/2000, -15, '目标频段', 'Color', 'r', 'HorizontalAlignment', 'center', 'FontSize', 12);

saveas(gcf, 'result_proof.png');

fprintf('所有流程运行完毕！\n');