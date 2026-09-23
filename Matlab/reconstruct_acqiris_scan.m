%% Acqiris单帧/多帧重建：从配置侧文件读取窗口，逐行处理，不整体加载原始文件
% 中文：ACQ_DATA_FILE指定输入；ACQ_FRAME_INDICES为逗号分隔帧号，空或all表示全部。
clear; clc;
dataFolder = 'D:\Acq_Storage';
dataFile = getenv('ACQ_DATA_FILE');
if isempty(dataFile)
    files = dir(fullfile(dataFolder,'BNU_Mark1_*.dat'));
    [~,order] = sort([files.datenum],'descend');
    for k=order
        candidate = fullfile(files(k).folder,files(k).name);
        configFile = [candidate '.config.json'];
        if isfile(configFile)
            c = jsondecode(fileread(configFile));
            if ~strcmp(c.status,'complete') || ~c.dataSaved, continue; end
        end
        if files(k).bytes==0, continue; end
        dataFile = candidate; break;
    end
end
assert(~isempty(dataFile),'没有完整采集文件');
% 中文：兼容旧9104us文件；新版优先读取真实记录配置，禁止按新窗口误读旧文件。
sampleRate = 1e9; linePeriod = 9104e-6; activeDuty = 0.9; frameFlyback = 1e-3;
pixelsPerLine = 512; linesPerFrame = pixelsPerLine;
activeLineSamples = round(linePeriod*activeDuty*sampleRate);
recordSamples = activeLineSamples; activeSampleOffset = 0;
config = struct;
if isfile([dataFile '.config.json'])
    config = jsondecode(fileread([dataFile '.config.json']));
    assert(strcmp(config.status,'complete') && config.dataSaved,'失败或不落盘诊断不能重建为完整采集');
    sampleRate = config.sampleRate; linePeriod = config.linePeriod;
    if isfield(config,'frameFlyback'), frameFlyback = config.frameFlyback; end
    recordSamples = config.recordSize;
    activeLineSamples = recordSamples; % 中文：旧文件无保护尾段，保持旧布局。
    if isfield(config,'activeLineSamples'), activeLineSamples = config.activeLineSamples; end
    if isfield(config,'activeSampleOffset'), activeSampleOffset = config.activeSampleOffset; end
    pixelsPerLine = config.pixelsPerLine; linesPerFrame = config.linesPerFrame;
    activeDuty = activeLineSamples/sampleRate/linePeriod;
end
assert(activeLineSamples>=pixelsPerLine && activeSampleOffset>=0 && ...
    activeSampleOffset+activeLineSamples<=recordSamples,'有效扫描段超出记录');
lineRate = 1/linePeriod; framePeriod = linesPerFrame*linePeriod+frameFlyback;
frameRate = 1/framePeriod;
samplesPerPixel = activeLineSamples/pixelsPerLine;
pixelEdges = round(linspace(0,activeLineSamples,pixelsPerLine+1));
pixelCounts = diff(pixelEdges);
info = dir(dataFile);
assert(isscalar(info) && mod(info.bytes,recordSamples*2)==0,'文件不是完整记录');
availableLines = info.bytes/(recordSamples*2);
partialFrameLines = mod(availableLines,linesPerFrame);
if partialFrameLines>0
    warning('固定时长或非整帧采集：末尾%d行保留在原文件，不重建为完整帧。',partialFrameLines);
end
availableFrames = floor(availableLines/linesPerFrame);
assert(availableFrames>=1,'文件不足一帧');
selection = getenv('ACQ_FRAME_INDICES');
if isempty(selection) || strcmpi(selection,'all')
    frameIndices = 1:availableFrames;
else
    frameIndices = str2double(strsplit(selection,','));
    assert(all(isfinite(frameIndices) & frameIndices>=1 & frameIndices<=availableFrames & frameIndices==floor(frameIndices)));
end
markers = table;
timingAnomalies = 0;
if isfile([dataFile '.markers.csv'])
    markers = readtable([dataFile '.markers.csv']);
    assert(height(markers)==availableLines,'marker与文件行数不符');
    assert(all(mod(diff(markers.record_index),2^24)==1),'marker索引不连续');
    tolerance = 2e-6;
    if isfield(config,'timingToleranceSeconds'), tolerance = config.timingToleranceSeconds; end
    rowsAll = (2:availableLines).';
    expectedAll = linePeriod+double(mod(rowsAll-1,linesPerFrame)==0)*frameFlyback;
    timingAnomalies = nnz(abs(markers.interval_s(2:end)-expectedAll)>tolerance);
end
% 中文：禁止把重复触发/错帧的数据默认为有效图像，诊断预览须显式开启。
if isfield(config,'timingAnomalies'), timingAnomalies = max(timingAnomalies,config.timingAnomalies); end
diagnosticPreview = strcmp(getenv('ACQ_ALLOW_TIMING_ANOMALIES'),'1');
assert(timingAnomalies==0 || diagnosticPreview, ...
    '存在%d个行/帧时序异常，拒绝正式重建。仅诊断可设ACQ_ALLOW_TIMING_ANOMALIES=1；不会自动删行。',timingAnomalies);
if isfield(config,'alignmentVerified') && ~config.alignmentVerified
    warning('首行相位未由外部信号验证；即使间隔正常，也不能证明首个激光沿或扫描帧起点。');
end
fid = fopen(dataFile,'rb','ieee-le');
assert(fid>=0); cleanup = onCleanup(@() fclose(fid));
images = zeros(linesPerFrame,pixelsPerLine,numel(frameIndices),'single');
frameStats = cell(1,numel(frameIndices));
waveform = [];
fprintf('INPUT %s | available_frames=%d | selected=%d | record=%d | active=%d | guard=%d\n', ...
    dataFile,availableFrames,numel(frameIndices),recordSamples,activeLineSamples,recordSamples-activeLineSamples);
startTime = tic;
for fi = 1:numel(frameIndices)
    frameIndex = frameIndices(fi);
    assert(fseek(fid,(frameIndex-1)*linesPerFrame*recordSamples*2,'bof')==0);
    image = zeros(linesPerFrame,pixelsPerLine);
    rawMinimum = inf; rawMaximum = -inf; lowRailCount = 0; highRailCount = 0;
    for line = 1:linesPerFrame
        record = fread(fid,recordSamples,'*int16');
        assert(numel(record)==recordSamples,'不完整record');
        % 中文：按完整record步进读取，仅对有效段积分，保护尾段不混入最后像素。
        raw = record(activeSampleOffset+(1:activeLineSamples));
        if samplesPerPixel==floor(samplesPerPixel)
            % 中文：新配置每像素12000点可直接分箱，省去大型double前缀和。
            image(line,:) = mean(reshape(raw,samplesPerPixel,pixelsPerLine),1);
        else
            sums = [0;cumsum(double(raw))];
            image(line,:) = (diff(sums(pixelEdges+1))./pixelCounts(:)).';
        end
        rawMinimum = min(rawMinimum,double(min(raw))); rawMaximum = max(rawMaximum,double(max(raw)));
        lowRailCount = lowRailCount+nnz(raw==-32768); highRailCount = highRailCount+nnz(raw==32767);
        if line==1
            probe = double(raw(1:min(8000000,numel(raw)))); % 中文：覆盖1kHz多个周期。
            sorted = sort(probe);
            lowLevelCode = median(sorted(1:floor(end/4)));
            highLevelCode = median(sorted(ceil(3*end/4):end));
            % 中文：迟滞交越排除慢斜坡上的噪声重复穿越，避免100kHz误报200kHz。
            lower = lowLevelCode+0.35*(highLevelCode-lowLevelCode);
            upper = lowLevelCode+0.65*(highLevelCode-lowLevelCode);
            highEvents = find(diff(probe>upper)==1);
            lowEvents = find(diff(probe<lower)==1);
            events = [highEvents;lowEvents]; labels = [ones(size(highEvents));zeros(size(lowEvents))];
            [events,order] = sort(events); labels = labels(order);
            rising = events(labels==1 & [true;diff(labels)~=0]);
            periods = diff(rising);
            measuredFrequency = NaN;
            if ~isempty(periods), measuredFrequency = sampleRate/median(periods); end
            if fi==1, waveform = probe(1:min(100000,numel(probe))); end
        end
    end
    stats = struct('inputFile',dataFile,'frameIndex',frameIndex,'imageSize',size(image), ...
        'diagnosticPreview',diagnosticPreview,'timingAnomalies',timingAnomalies, ...
        'recordSamples',recordSamples,'activeLineSamples',activeLineSamples,'partialFrameLines',partialFrameLines, ...
        'meanCode',mean(image(:)),'stdCode',std(image(:)), ...
        'minPixelCode',min(image(:)),'maxPixelCode',max(image(:)), ...
        'rawMinimum',rawMinimum,'rawMaximum',rawMaximum, ...
        'lowRailFraction',lowRailCount/(activeLineSamples*linesPerFrame), ...
        'highRailFraction',highRailCount/(activeLineSamples*linesPerFrame), ...
        'estimatedFrequencyHz',measuredFrequency,'cyclesPerPixel',samplesPerPixel/sampleRate*measuredFrequency);
    if ~isempty(markers)
        rows = (frameIndex-1)*linesPerFrame+(1:linesPerFrame);
        checkedRows = rows(rows>1); % 中文：包括跨帧第一行，修复此前永远检查不到帧边界的问题。
        delta = markers.interval_s(checkedRows);
        expected = repmat(linePeriod,size(delta));
        boundaryRows = mod(checkedRows-1,linesPerFrame)==0;
        expected(boundaryRows) = linePeriod+frameFlyback;
        stats.markerMinInterval = min(delta); stats.markerMaxInterval = max(delta);
        stats.markerUnusualIntervals = nnz(abs(delta-expected)>tolerance);
        stats.frameBoundaryIntervals = nnz(boundaryRows);
    end
    images(:,:,fi) = single(image); frameStats{fi} = stats;
    [folder,stem] = fileparts(dataFile);
    base = fullfile(folder,sprintf('%s_frame%04d',stem,frameIndex));
    if diagnosticPreview, base = [base '_DIAGNOSTIC']; end
    imwrite(uint16(round(image+32768)),[base '.png']);
    save([base '.mat'],'image','stats','config','sampleRate','linePeriod','frameFlyback','lineRate','frameRate', ...
        'recordSamples','activeSampleOffset','activeLineSamples','samplesPerPixel','pixelEdges','pixelCounts','activeDuty','pixelsPerLine','linesPerFrame');
    jf = fopen([base '_summary.json'],'w','n','UTF-8');
    assert(jf>=0); fprintf(jf,'%s\n',jsonencode(stats,PrettyPrint=true)); fclose(jf);
    fprintf('FRAME_RESULT %s\n',jsonencode(stats));
end
clear cleanup;
% 中文：多帧增强统一使用同一色标，避免每帧自动拉伸掩盖帧间变化。
lo = double(min(images(:))); hi = double(max(images(:)));
for fi=1:numel(frameIndices)
    enhanced = (double(images(:,:,fi))-lo)/max(hi-lo,eps);
    diagnosticSuffix = ''; if diagnosticPreview, diagnosticSuffix = '_DIAGNOSTIC'; end
    imwrite(uint16(round(enhanced*65535)),fullfile(folder,sprintf('%s_frame%04d%s_contrast.png',stem,frameIndices(fi),diagnosticSuffix)));
end
baseAll = fullfile(folder,[stem '_multiframe']);
if diagnosticPreview, baseAll = [baseAll '_DIAGNOSTIC']; end
save([baseAll '.mat'],'images','frameStats','frameIndices','markers','config','waveform','sampleRate','linePeriod','frameFlyback','framePeriod','samplesPerPixel');
fig = figure('Visible','off','Color','w','Position',[50 50 1500 520]);
tiledlayout(1,3);
nexttile; imagesc(images(:,:,1),[lo max(hi,lo+eps)]); axis image; colormap gray; colorbar;
title(sprintf('Frame %d | common enhanced scale',frameIndices(1)));
if diagnosticPreview, title('DIAGNOSTIC ONLY - timing not validated'); end
nexttile; imagesc(images(:,:,end),[lo max(hi,lo+eps)]); axis image; colorbar;
title(sprintf('Frame %d | common enhanced scale',frameIndices(end)));
nexttile; plot((0:numel(waveform)-1)/sampleRate*1e6,waveform); grid on;
    displayFrequency = frameStats{1}.estimatedFrequencyHz;
    if ~isfinite(displayFrequency) || displayFrequency<=0, displayFrequency=1; end
    xlim([0 min(100,5e6/displayFrequency)]);
xlabel('Time (us)'); ylabel('ADC code');
title(sprintf('Raw waveform | %.4f kHz',frameStats{1}.estimatedFrequencyHz/1e3));
exportgraphics(fig,[baseAll '_preview.png'],'Resolution',120); close(fig);
summary = struct('inputFile',dataFile,'fileBytes',info.bytes,'availableFrames',availableFrames, ...
    'partialFrameLines',partialFrameLines,'timingAnomalies',timingAnomalies,'diagnosticPreview',diagnosticPreview, ...
    'frameIndices',frameIndices,'reconstructionSeconds',toc(startTime),'frameStats',{frameStats});
jf = fopen([baseAll '_summary.json'],'w','n','UTF-8');
assert(jf>=0); fprintf(jf,'%s\n',jsonencode(summary,PrettyPrint=true)); fclose(jf);
fprintf('MULTIFRAME_RESULT frames=%d seconds=%.3f output=%s\n',numel(frameIndices),toc(startTime),baseAll);
