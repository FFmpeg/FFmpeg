module.exports = async ({github, context}) => {
    const title = (context.payload.pull_request?.title || context.payload.issue?.title || '').toLowerCase();
    const labels = [];

    const kwmap = {
      'avcodec': 'avcodec',
      'avdevice': 'avdevice',
      'avfilter': 'avfilter',
      'avformat': 'avformat',
      'avutil': 'avutil',
      'swresample': 'swresample',
      'swscale': 'swscale',
      'fftools': 'CLI'
    };

    if (context.payload.action === 'opened') {
        labels.push('new');
        console.log('Detected label: new');
    }

    for (const [kw, label] of Object.entries(kwmap)) {
        if (title.includes(kw)) {
            labels.push(label);
            console.log('Detected label: ' + label);
        }
    }

    if (labels.length > 0) {
        await github.rest.issues.addLabels({
            owner: context.repo.owner,
            repo: context.repo.repo,
            issue_number: context.payload.pull_request?.number || context.payload.issue?.number,
            labels: labels,
        });
    }
}
