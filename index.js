const fs = require('fs');
const ffmpeg = require('./build/Release/ffmpeg');

const d = fs.readFileSync('./sample.mkv');
ffmpeg.loadVideo(d);

module.exports = ffmpeg;
