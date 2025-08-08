module.exports = async ({github, context}) => {
    const title = (context.payload.pull_request?.title || context.payload.issue?.title || '').toLowerCase();
    const labels = [];
    const issueNumber = context.payload.pull_request?.number || context.payload.issue?.number;

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

    async function isOrgMember(username) {
        try {
            const response = await github.rest.orgs.checkMembershipForUser({
                org: context.repo.owner,
                username: username
            });
            return response.status === 204;
        } catch (error) {
            return false;
        }
    }

    if (context.payload.action === 'closed' ||
        (context.payload.action !== 'opened' && (
             context.payload.action === 'assigned' ||
             context.payload.action === 'label_updated' ||
             context.payload.comment) &&
         await isOrgMember(context.payload.sender.login))
    ) {
        try {
            await github.rest.issues.removeLabel({
                owner: context.repo.owner,
                repo: context.repo.repo,
                issue_number: issueNumber,
                // this should say 'new', but forgejo deviates from GitHub API here and expects the ID
                name: '41'
            });
            console.log('Removed "new" label');
        } catch (error) {
            if (error.status !== 404 && error.status !== 410) {
                console.log('Could not remove "new" label');
            }
        }
    } else if (context.payload.action === 'opened') {
        labels.push('new');
        console.log('Detected label: new');
    }

    if ((context.payload.action === 'opened' || context.payload.action === 'edited') && context.eventName !== 'issue_comment') {
        for (const [kw, label] of Object.entries(kwmap)) {
            if (title.includes(kw)) {
                labels.push(label);
                console.log('Detected label: ' + label);
            }
        }
    }

    if (labels.length > 0) {
        await github.rest.issues.addLabels({
            owner: context.repo.owner,
            repo: context.repo.repo,
            issue_number: issueNumber,
            labels: labels,
        });
    }
}
